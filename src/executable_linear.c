#include "executable_internal.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* IBM LX sections 2.11-2.14; LE uses the same import/fixup encoding.
   Table offsets are relative to the linear header, not the DOS stub. */
#define LINEAR_WORK_LIMIT 1000000u

typedef struct linearNames {
    size_t *offsets;
    size_t count;
    size_t capacity;
} linearNames;

static int linear_relative(size_t size, size_t header, uint32_t relative,
                            size_t *absolute) {
    if (!relative || !byte_span(size, header, relative)) return 0;
    *absolute = header + relative;
    return 1;
}

static int linear_name_add(linearNames *names, size_t offset, executableInfo *info) {
    if (names->count == LINEAR_WORK_LIMIT)
        return exe_error(info, EXE_LIMIT, "Too many LE/LX procedure names");
    if (names->count == names->capacity) {
        size_t capacity = names->capacity ? names->capacity * 2 : 32;
        size_t *p = realloc(names->offsets, capacity * sizeof(*p));
        if (!p) return exe_error(info, EXE_LIMIT, "Cannot allocate LE/LX names");
        names->offsets = p;
        names->capacity = capacity;
    }
    names->offsets[names->count++] = offset;
    return 1;
}

static int linear_name_exists(const linearNames *names, size_t offset) {
    size_t low = 0, high = names->count;
    while (low < high) {
        size_t mid = low + (high - low) / 2;
        if (names->offsets[mid] < offset) low = mid + 1;
        else high = mid;
    }
    return low < names->count && names->offsets[low] == offset;
}

static int linear_take(const uint8_t *data, size_t end, size_t *cursor,
                        unsigned width, uint32_t *value) {
    if (!byte_span(end, *cursor, width)) return 0;
    *value = width == 1 ? data[*cursor] : width == 2 ? read_le16(data + *cursor)
                                                   : read_le32(data + *cursor);
    *cursor += width;
    return 1;
}

static int linear_objects(const uint8_t *data, size_t size, size_t header,
                           executableInfo *info) {
    const uint8_t *h = data + header;
    size_t count = read_le32(h + 0x44), pages = read_le32(h + 0x14);
    size_t table = 0, page_table = 0;
    size_t page_stride = h[1] == 'X' ? 8 : 4;
    if (count > LINEAR_WORK_LIMIT || pages > LINEAR_WORK_LIMIT)
        return exe_error(info, EXE_LIMIT, "Too many LE/LX objects or pages");
    if ((count && (!linear_relative(size, header, read_le32(h + 0x40), &table) ||
                   table < header + 0xb0 || count > (size - table) / 24)) ||
        (pages && (!linear_relative(size, header, read_le32(h + 0x48), &page_table) ||
                   page_table < header + 0xb0 || pages > (size - page_table) / page_stride)))
        return exe_error(info, EXE_MALFORMED, "Invalid LE/LX object/page table");
    if (byte_overlap(table, count * 24, page_table, pages * page_stride))
        return exe_error(info, EXE_MALFORMED, "Overlapping LE/LX object/page tables");
    for (size_t i = 0; i < count; ++i) {
        size_t offset = table + i * 24;
        const uint8_t *p = data + offset;
        uint32_t page = read_le32(p + 12), n = read_le32(p + 16);
        if (n && (!page || page > pages || n > pages - (page - 1)))
            return exe_error(info, EXE_MALFORMED, "Invalid LE/LX object page range");
        if (!exe_add(info, EXE_REGION, offset, 24,
                     "Object %zu: VA %08X size %08X flags %08X pages %u+%u",
                     i + 1, read_le32(p + 4), read_le32(p), read_le32(p + 8), page, n)) return 0;
    }
    return 1;
}

static int linear_record(const uint8_t *data, size_t size, size_t end,
                          size_t *cursor, const size_t *modules, size_t module_count,
                          size_t procedures, size_t fixup_end,
                          const linearNames *names, int editable,
                          executableInfo *info) {
    size_t record = *cursor;
    uint32_t src, flags, source, index, target = 0, ignored;
    if (!linear_take(data, end, cursor, 1, &src) ||
        !linear_take(data, end, cursor, 1, &flags)) goto malformed;
    unsigned source_type = src & 15, target_type = flags & 3;
    if ((src & 0xc0) || source_type == 1 || source_type == 4 || source_type > 8)
        return exe_error(info, EXE_UNSUPPORTED, "Unknown LE/LX fixup source type");
    if ((flags & 8) && (source_type != 7 || (src & 0x20) ||
                       (target_type != 0 && target_type != 3))) goto malformed;
    if (!linear_take(data, end, cursor, src & 0x20 ? 1 : 2, &source) ||
        !linear_take(data, end, cursor, flags & 0x40 ? 2 : 1, &index)) goto malformed;
    size_t target_offset = *cursor;
    unsigned width = flags & 0x10 ? 4 : 2;
    if (target_type == 1 && (flags & 0x80)) width = 1;
    if ((target_type == 0 && source_type != 2) || target_type == 1 || target_type == 2) {
        if (!linear_take(data, end, cursor, width, &target)) goto malformed;
    }
    if (flags & 4)
        if (!linear_take(data, end, cursor, flags & 0x20 ? 4 : 2, &ignored)) goto malformed;
    if (src & 0x20) {
        if (!source || !byte_span(end, *cursor, source * 2)) goto malformed;
        *cursor += source * 2;
    }
    if (target_type != 1 && target_type != 2) return 1;
    if (!index || index > module_count) goto malformed;
    char module[256], procedure[256];
    size_t chars, length;
    if (!exe_name(data, size, modules[index - 1], fixup_end, 1,
                  module, &chars, &length)) goto malformed;
    size_t name_offset = SIZE_MAX, name_length = 0;
    executableRow *row;
    if (target_type == 2) {
        if (procedures == SIZE_MAX || target > fixup_end - procedures ||
            !linear_name_exists(names, procedures + target) ||
            !exe_name(data, size, procedures + target, fixup_end, 1,
                      procedure, &name_offset, &name_length) || !name_length) goto malformed;
        row = exe_add(info, EXE_IMPORT, record, *cursor - record, "%s!%s", module, procedure);
    } else {
        row = exe_add(info, EXE_IMPORT, record, *cursor - record, "%s!#%u", module, target);
    }
    if (!row) return 0;
    row->editable = editable;
    if (target_type == 2) {
        row->name_offset = name_offset;
        row->name_length = name_length;
    } else {
        row->ordinal_offset = target_offset;
        row->ordinal_width = width;
        row->ordinal = target;
        row->ordinal_max = width == 1 ? UINT8_MAX : width == 2 ? UINT16_MAX : UINT32_MAX;
    }
    return 1;
malformed:
    return exe_error(info, EXE_MALFORMED, "Invalid or truncated LE/LX fixup record");
}

int exe_parse_linear(const uint8_t *data, size_t size, size_t header,
                      executableInfo *info) {
    if (!byte_span(size, header, 0xb0))
        return exe_error(info, EXE_MALFORMED, "Truncated LE/LX header");
    const uint8_t *h = data + header;
    snprintf(info->format, sizeof(info->format), "%c%c", h[0], h[1]);
    if (h[2] || h[3] || read_le32(h + 4))
        return exe_error(info, EXE_UNSUPPORTED, "Unsupported LE/LX byte order or format level");
    if (!exe_add(info, EXE_HEADER, header, 0xb0, "%c%c header: CPU %u OS %u flags %08X",
                 h[0], h[1], read_le16(h + 8), read_le16(h + 10), read_le32(h + 16)) ||
        !linear_objects(data, size, header, info)) return 0;

    uint32_t section_size = read_le32(h + 0x30), count = read_le32(h + 0x74);
    size_t pages = read_le32(h + 0x14);
    size_t fixups = 0, records = 0, module_table = SIZE_MAX, procedures = SIZE_MAX;
    if (!section_size && !count && !read_le32(h + 0x68) &&
        !read_le32(h + 0x6c) && !read_le32(h + 0x70) && !read_le32(h + 0x78)) return 1;
    if (count > UINT16_MAX)
        return exe_error(info, EXE_LIMIT, "Too many LE/LX import modules");
    if (!linear_relative(size, header, read_le32(h + 0x68), &fixups) ||
        fixups < header + 0xb0 || !byte_span(size, fixups, section_size) ||
        !linear_relative(size, header, read_le32(h + 0x6c), &records))
        return exe_error(info, EXE_MALFORMED, "Invalid LE/LX fixup section");
    size_t fixup_end = fixups + section_size;
    size_t object_count = read_le32(h + 0x44);
    /* Import edits must never alias loader records, even when the aliased
       bytes also happen to form syntactically valid objects or page maps. */
    if ((object_count && byte_overlap(header + read_le32(h + 0x40), object_count * 24,
                                        fixups, section_size)) ||
        (pages && byte_overlap(header + read_le32(h + 0x48), pages * (h[1] == 'X' ? 8 : 4),
                                 fixups, section_size)))
        return exe_error(info, EXE_MALFORMED, "LE/LX fixup section overlaps loader tables");
    if (read_le32(h + 0x70) &&
        !linear_relative(size, header, read_le32(h + 0x70), &module_table)) goto malformed;
    if (read_le32(h + 0x78) &&
        !linear_relative(size, header, read_le32(h + 0x78), &procedures)) goto malformed;
    if (records < fixups || records > fixup_end || pages >= (records - fixups) / 4 ||
        (module_table != SIZE_MAX && (module_table < records || module_table > fixup_end)) ||
        (procedures != SIZE_MAX && (procedures < records || procedures > fixup_end)) ||
        (module_table != SIZE_MAX && procedures != SIZE_MAX && module_table > procedures) ||
        (count && (module_table == SIZE_MAX || procedures == SIZE_MAX))) goto malformed;
    size_t record_end = module_table != SIZE_MAX ? module_table :
                        procedures != SIZE_MAX ? procedures : fixup_end;
    if (read_le32(data + fixups)) goto malformed;
    size_t *modules = count ? malloc((size_t)count * sizeof(*modules)) : NULL;
    linearNames names = {0};
    if (count && !modules)
        return exe_error(info, EXE_LIMIT, "Cannot allocate LE/LX modules");
    int success = 0;
    int editable = read_le32(h + 0x34) == 0;
    size_t cursor = module_table;
    for (uint32_t i = 0; i < count; ++i) {
        char name[256];
        size_t chars, length;
        if (!exe_name(data, size, cursor, procedures, 1, name, &chars, &length) || !length)
            goto malformed_names;
        modules[i] = cursor;
        executableRow *row = exe_add(info, EXE_MODULE, cursor, length + 1,
                                     "Module %u: %s", i + 1, name);
        if (!row) goto done;
        row->name_offset = chars;
        row->name_length = length;
        row->editable = editable;
        cursor = chars + length;
    }
    if (count && cursor != procedures) goto malformed_names;
    if (procedures != SIZE_MAX) {
        cursor = procedures;
        if (cursor == fixup_end || data[cursor]) goto malformed_names;
        while (cursor < fixup_end) {
            if (data[cursor] & 0x80) {
                exe_error(info, EXE_UNSUPPORTED, "LE/LX overloaded procedure names are unsupported");
                goto done;
            }
            size_t length = data[cursor];
            if (!byte_span(fixup_end, cursor + 1, length)) goto malformed_names;
            if (!linear_name_add(&names, cursor, info)) goto done;
            cursor += length + 1;
        }
    }
    size_t work = 0;
    for (size_t page = 0; page < pages; ++page) {
        uint32_t begin = read_le32(data + fixups + page * 4);
        uint32_t end = read_le32(data + fixups + (page + 1) * 4);
        if (begin > end || end > record_end - records) goto malformed_names;
        cursor = records + begin;
        while (cursor < records + end) {
            if (++work > LINEAR_WORK_LIMIT) {
                exe_error(info, EXE_LIMIT, "Too many LE/LX fixup records");
                goto done;
            }
            if (!linear_record(data, size, records + end, &cursor, modules, count,
                               procedures, fixup_end, &names, editable, info)) goto done;
        }
    }
    if (!editable)
        snprintf(info->message, sizeof(info->message),
                 "Imports are read-only: nonzero fixup checksum is not regenerated");
    success = 1;
    goto done;
malformed_names:
    exe_error(info, EXE_MALFORMED, "Invalid LE/LX import tables or page boundaries");
done:
    free(modules);
    free(names.offsets);
    return success;
malformed:
    return exe_error(info, EXE_MALFORMED, "Invalid LE/LX import table layout");
}
