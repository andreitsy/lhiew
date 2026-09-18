#include "executable_internal.h"

#include <limits.h>
#include <stdlib.h>
#include <string.h>

/* NE layout and fixup semantics:
 * https://raw.githubusercontent.com/open-watcom/open-watcom-v2/master/bld/watcom/h/exeos2.h
 * https://raw.githubusercontent.com/wine-mirror/wine/master/dlls/krnl386.exe16/ne_segment.c
 * The imported-name pool is shared by module references and name fixups.
 * A name's character bytes can be changed without moving any records. */

static int ne_name(const uint8_t *data, size_t size, size_t pool, size_t end,
                   const uint8_t starts[8192], unsigned relative,
                   char name[256], size_t *offset, size_t *length) {
    if (relative >= end - pool || !(starts[relative / 8] & (1u << (relative % 8))))
        return 0;
    return exe_name(data, size, pool + relative, end, 1, name, offset, length) && *length;
}

static int ne_module_name(const uint8_t *data, size_t size, size_t modules,
                          size_t count, unsigned index, size_t pool, size_t end,
                          const uint8_t starts[8192], char name[256],
                          size_t *offset, size_t *length) {
    if (!index || index > count) return 0;
    return ne_name(data, size, pool, end, starts,
                   read_le16(data + modules + (index - 1) * 2),
                   name, offset, length);
}

typedef struct neFileSpan {
    size_t start, end;
} neFileSpan;

static int ne_span_compare(const void *a, const void *b) {
    const neFileSpan *left = a, *right = b;
    return left->start < right->start ? -1 : left->start > right->start;
}

/* A relocation field must not also be another segment's code/data. Build
   one span per file-backed segment including its trailing relocation table,
   then check adjacency after sorting rather than comparing every pair. */
static int ne_segments_disjoint(const uint8_t *data, size_t size, size_t table,
                                size_t segments, unsigned shift, size_t metadata_end,
                                executableInfo *info) {
    if (!segments) return 1;
    neFileSpan *spans = malloc(segments * sizeof(*spans));
    if (!spans)
        return exe_error(info, EXE_LIMIT, "Cannot allocate NE segment ranges");
    size_t count = 0;
    for (size_t i = 0; i < segments; ++i) {
        const uint8_t *record = data + table + i * 8;
        size_t sector = read_le16(record), length = read_le16(record + 2);
        unsigned flags = read_le16(record + 4);
        if (!sector) {
            if (flags & 0x100) goto malformed;
            continue;
        }
        if (sector > (SIZE_MAX >> shift)) goto malformed;
        size_t offset = sector << shift;
        if (!length) length = 65536;
        if (offset < metadata_end || !byte_span(size, offset, length)) goto malformed;
        size_t end = offset + length;
        if (flags & 0x100) {
            if (!byte_span(size, end, 2)) goto malformed;
            size_t relocations = read_le16(data + end);
            if (!byte_span(size, end + 2, relocations * 8)) goto malformed;
            end += 2 + relocations * 8;
        }
        spans[count++] = (neFileSpan){offset, end};
    }
    if (count > 1) qsort(spans, count, sizeof(*spans), ne_span_compare);
    for (size_t i = 1; i < count; ++i)
        if (spans[i].start < spans[i - 1].end) goto malformed;
    free(spans);
    return 1;
malformed:
    free(spans);
    return exe_error(info, EXE_MALFORMED, "Invalid or overlapping NE segment/relocation spans");
}

int exe_parse_ne(const uint8_t *data, size_t size, size_t header, executableInfo *info) {
    snprintf(info->format, sizeof(info->format), "NE");
    if (!byte_span(size, header, 64))
        return exe_error(info, EXE_MALFORMED, "Truncated NE header");
    const uint8_t *h = data + header;
    if (!exe_add(info, EXE_HEADER, header, 64, "NE header")) return 0;
    if ((read_le16(h + 12) & 0x0800) && (h[54] == 2 || h[54] == 4))
        return exe_error(info, EXE_UNSUPPORTED, "Self-loading NE modules are unsupported");

    size_t segments = read_le16(h + 28);
    size_t modules = read_le16(h + 30);
    size_t segment_table = read_le16(h + 34);
    size_t module_table = read_le16(h + 40);
    size_t names = read_le16(h + 42);
    size_t entry = read_le16(h + 4);
    size_t entry_length = read_le16(h + 6);
    unsigned shift = read_le16(h + 50);
    size_t remaining = size - header;
    if (shift >= sizeof(size_t) * CHAR_BIT ||
        (segments && (segment_table < 64 || !byte_span(remaining, segment_table, segments * 8))) ||
        (modules && (module_table < 64 || !byte_span(remaining, module_table, modules * 2))) ||
        (entry_length && (entry < 64 || !byte_span(remaining, entry, entry_length))))
        return exe_error(info, EXE_MALFORMED, "Invalid NE table or segment alignment");

    /* Standard NE puts the name pool immediately before the entry table.
     * Reject overlapping metadata rather than offering unsafe semantic edits. */
    if (modules && (names < module_table + modules * 2 || entry <= names ||
                    !byte_span(remaining, names, entry - names)))
        return exe_error(info, EXE_MALFORMED, "Invalid NE imported-name pool");
    if (segments && modules && segment_table + segments * 8 > module_table)
        return exe_error(info, EXE_MALFORMED, "Overlapping NE metadata tables");
    size_t metadata_end = 64;
    if (segments && segment_table + segments * 8 > metadata_end)
        metadata_end = segment_table + segments * 8;
    if (modules && entry > metadata_end) metadata_end = entry;
    if (entry_length && entry + entry_length > metadata_end) metadata_end = entry + entry_length;
    metadata_end += header;
    size_t pool = header + names;
    size_t pool_end = modules ? header + entry : pool;
    segment_table += header;
    module_table += header;
    if (!ne_segments_disjoint(data, size, segment_table, segments, shift, metadata_end, info))
        return 0;

    /* Mark complete strings once. This also rejects references into the
     * middle of another string, which would make an in-place edit ambiguous. */
    uint8_t starts[8192] = {0};
    for (size_t at = pool; at < pool_end;) {
        size_t length = data[at];
        if (!byte_span(pool_end, at + 1, length))
            return exe_error(info, EXE_MALFORMED, "Truncated NE imported name");
        size_t relative = at - pool;
        starts[relative / 8] |= (uint8_t)(1u << (relative % 8));
        at += length + 1;
    }

    for (size_t i = 0; i < modules; ++i) {
        char name[256];
        size_t name_offset, name_length;
        if (!ne_module_name(data, size, module_table, modules, (unsigned)i + 1,
                             pool, pool_end, starts, name, &name_offset, &name_length))
            return exe_error(info, EXE_MALFORMED, "Invalid NE module name reference");
        executableRow *row = exe_add(info, EXE_MODULE, module_table + i * 2, 2,
                                     "Module %zu: %s", i + 1, name);
        if (!row) return 0;
        row->name_offset = name_offset;
        row->name_length = name_length;
        row->editable = 1;
    }

    /* ne_segments_disjoint already validated all segment and relocation spans. */
    size_t examined = 0;
    for (size_t i = 0; i < segments; ++i) {
        size_t record = segment_table + i * 8;
        size_t sector = read_le16(data + record);
        size_t length = read_le16(data + record + 2);
        unsigned flags = read_le16(data + record + 4);
        if (!sector) {
            if (!exe_add(info, EXE_REGION, record, 8, "Segment %zu: no file data", i + 1)) return 0;
            continue;
        }
        size_t offset = sector << shift;
        if (!length) length = 65536;
        if (!exe_add(info, EXE_REGION, offset, length, "Segment %zu: %s%s", i + 1,
                     flags & 1 ? "data" : "code", flags & 8 ? " (iterated)" : "")) return 0;
        if (!(flags & 0x100)) continue;
        size_t relocations = offset + length;
        size_t count = read_le16(data + relocations);
        relocations += 2;
        if (count > 1000000 - examined)
            return exe_error(info, EXE_LIMIT, "NE relocation inspection limit reached");
        examined += count;
        for (size_t j = 0; j < count; ++j) {
            size_t relocation = relocations + j * 8;
            unsigned kind = data[relocation + 1] & 3;
            if (kind != 1 && kind != 2) continue;
            char module[256], name[256];
            size_t name_offset, name_length;
            unsigned module_index = read_le16(data + relocation + 4);
            if (!ne_module_name(data, size, module_table, modules, module_index,
                                 pool, pool_end, starts, module, &name_offset, &name_length))
                return exe_error(info, EXE_MALFORMED, "Invalid NE import module index");
            unsigned target = read_le16(data + relocation + 6);
            executableRow *row;
            if (kind == 2) {
                if (!ne_name(data, size, pool, pool_end, starts, target,
                             name, &name_offset, &name_length))
                    return exe_error(info, EXE_MALFORMED, "Invalid NE import name reference");
                row = exe_add(info, EXE_IMPORT, relocation, 8,
                              "%s!%s [segment %zu]", module, name, i + 1);
            } else {
                row = exe_add(info, EXE_IMPORT, relocation, 8,
                              "%s!#%u [segment %zu]", module, target, i + 1);
            }
            if (!row) return 0;
            if (kind == 2) {
                row->name_offset = name_offset;
                row->name_length = name_length;
            } else {
                row->ordinal_offset = relocation + 6;
                row->ordinal_width = 2;
                row->ordinal = target;
                row->ordinal_max = UINT16_MAX;
            }
            row->editable = 1;
        }
    }
    return 1;
}
