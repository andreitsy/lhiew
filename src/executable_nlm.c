#include "executable_internal.h"

#include <stdio.h>
#include <string.h>

/* NetWare's i386 v4 layout, documented by Open Watcom's exenov.h and
   loadnov.c. Other NLM CPU variants use different headers/relocations. */
#define NLM_HEADER_SIZE 130
#define NLM_RECORD_LIMIT 100000

typedef struct nlmSpan {
    size_t start;
    size_t length;
} nlmSpan;

static int nlm_bad(executableInfo *info) {
    return exe_error(info, EXE_MALFORMED, "Invalid or truncated NLM header/table");
}

static int nlm_count(executableInfo *info, size_t count) {
    if (count > NLM_RECORD_LIMIT)
        return exe_error(info, EXE_LIMIT, "NLM table exceeds the 100000-record inspection limit");
    return 1;
}

static int nlm_range(size_t size, size_t offset, size_t length) {
    return exe_span(size, offset, length) && (!length || offset >= NLM_HEADER_SIZE);
}

static int nlm_fixup(uint32_t value, size_t code_size, size_t data_size) {
    size_t offset = value & UINT32_C(0x3fffffff);
    return exe_span(value & UINT32_C(0x40000000) ? code_size : data_size, offset, 4);
}

static int nlm_name(const uint8_t *data, size_t size, size_t cursor,
                    char name[256], size_t *chars, size_t *length) {
    return exe_name(data, size, cursor, size, 1, name, chars, length) &&
           *length && !memchr(data + *chars, 0, *length);
}

static int nlm_variable_header(const uint8_t *data, size_t size, size_t *end) {
    size_t cursor = NLM_HEADER_SIZE;
    const size_t maximum[] = {127, 71, 17};
    for (size_t i = 0; i < 3; ++i) {
        if (!exe_span(size, cursor, 1)) return 0;
        size_t length = data[cursor++];
        if (length > maximum[i] || !exe_span(size, cursor, length + 1) ||
            data[cursor + length]) return 0;
        cursor += length + 1;
        if (i == 0) { /* Stack size, reserved word and oldThreadName[6]. */
            if (!exe_span(size, cursor, 14)) return 0;
            cursor += 14;
        }
    }
    *end = cursor;
    return 1;
}

static int nlm_names(const uint8_t *data, size_t size, size_t start,
                     size_t count, executableInfo *info, nlmSpan *range) {
    if (!nlm_range(size, start, count)) return nlm_bad(info);
    if (!nlm_count(info, count)) return 0;
    size_t cursor = start;
    for (size_t i = 0; i < count; ++i) {
        char name[256], label[576];
        size_t chars, length;
        if (!nlm_name(data, size, cursor, name, &chars, &length))
            return nlm_bad(info);
        snprintf(label, sizeof(label), "Dependency: %s", name);
        executableRow *row = exe_add(info, EXE_MODULE, cursor, length + 1, label);
        if (!row) return 0;
        row->name_offset = chars;
        row->name_length = length;
        row->editable = 1;
        cursor = chars + length;
    }
    *range = (nlmSpan){start, cursor - start};
    return 1;
}

static int nlm_imports(const uint8_t *data, size_t size, size_t start,
                       size_t count, size_t code_size, size_t data_size,
                       executableInfo *info, nlmSpan *range) {
    if (!nlm_range(size, start, count) || count > (size - start) / 6)
        return nlm_bad(info);
    if (!nlm_count(info, count)) return 0;
    size_t cursor = start, total_references = 0;
    for (size_t i = 0; i < count; ++i) {
        char name[256], label[576];
        size_t chars, length;
        if (!nlm_name(data, size, cursor, name, &chars, &length))
            return nlm_bad(info);
        size_t refs_offset = chars + length;
        if (!exe_span(size, refs_offset, 4)) return nlm_bad(info);
        size_t refs = exe_u32(data + refs_offset);
        refs_offset += 4;
        if (refs > (size - refs_offset) / 4) return nlm_bad(info);
        if (refs > NLM_RECORD_LIMIT - total_references)
            return exe_error(info, EXE_LIMIT, "NLM exceeds the 100000-reference inspection limit");
        total_references += refs;
        for (size_t j = 0; j < refs; ++j)
            if (!nlm_fixup(exe_u32(data + refs_offset + j * 4), code_size, data_size))
                return nlm_bad(info);
        size_t end = refs_offset + refs * 4;
        snprintf(label, sizeof(label), "Import: %s [global; %zu references]", name, refs);
        executableRow *row = exe_add(info, EXE_IMPORT, cursor, end - cursor, label);
        if (!row) return 0;
        row->name_offset = chars;
        row->name_length = length;
        row->editable = 1;
        cursor = end;
    }
    *range = (nlmSpan){start, cursor - start};
    return 1;
}

/* Bound auxiliary tables as well: otherwise a malformed table could overlap
   an editable import name. Debug records use type/u32 offset/u8 name length. */
static int nlm_symbols(const uint8_t *data, size_t size, size_t start,
                       size_t count, size_t code_size, size_t data_size,
                       size_t bss_size, int debug, executableInfo *info,
                       nlmSpan *range) {
    size_t minimum = debug ? 7 : 6;
    if (!nlm_range(size, start, count) || count > (size - start) / minimum)
        return nlm_bad(info);
    if (!nlm_count(info, count)) return 0;
    size_t cursor = start;
    for (size_t i = 0; i < count; ++i) {
        char name[256];
        size_t chars, length;
        uint32_t address;
        int code;
        if (debug) {
            if (!exe_span(size, cursor, 6) || data[cursor] > 1) return nlm_bad(info);
            code = data[cursor];
            address = exe_u32(data + cursor + 1);
            cursor += 5;
            if (!nlm_name(data, size, cursor, name, &chars, &length))
                return nlm_bad(info);
            cursor = chars + length;
        } else {
            if (!nlm_name(data, size, cursor, name, &chars, &length))
                return nlm_bad(info);
            cursor = chars + length;
            if (!exe_span(size, cursor, 4)) return nlm_bad(info);
            address = exe_u32(data + cursor);
            code = !!(address & UINT32_C(0x80000000));
            address &= UINT32_C(0x7fffffff);
            cursor += 4;
        }
        uint64_t extent = code ? code_size : (uint64_t)data_size + bss_size;
        if (address > extent) return nlm_bad(info);
    }
    *range = (nlmSpan){start, cursor - start};
    return 1;
}

int exe_parse_nlm(const uint8_t *data, size_t size, executableInfo *info) {
    snprintf(info->format, sizeof(info->format), "NLM");
    if (size < 24) return nlm_bad(info);
    if (memcmp(data, "NetWare Loadable Module\x1a", 24))
        return exe_error(info, EXE_UNSUPPORTED, "Only i386 NLM v4 import tables are supported");
    if (size < NLM_HEADER_SIZE) return nlm_bad(info);
    if (exe_u32(data + 24) != 4)
        return exe_error(info, EXE_UNSUPPORTED, "Only i386 NLM v4 import tables are supported");
    size_t module_length = data[28];
    if (!module_length || module_length > 12 || data[29 + module_length]) return nlm_bad(info);
    if (memchr(data + 29, 0, module_length)) return nlm_bad(info);
    size_t header_end;
    if (!nlm_variable_header(data, size, &header_end)) return nlm_bad(info);
    size_t code_offset = exe_u32(data + 42), code_size = exe_u32(data + 46);
    size_t data_offset = exe_u32(data + 50), data_size = exe_u32(data + 54);
    size_t bss_size = exe_u32(data + 58);
    size_t custom_offset = exe_u32(data + 62), custom_size = exe_u32(data + 66);
    if (!nlm_range(size, code_offset, code_size) || !nlm_range(size, data_offset, data_size) ||
        !nlm_range(size, custom_offset, custom_size)) return nlm_bad(info);
    for (size_t field = 110; field <= 118; field += 4) {
        size_t address = exe_u32(data + field);
        if (address && address >= code_size) return nlm_bad(info);
    }
    char label[576];
    snprintf(label, sizeof(label), "NLM v4 i386: %.*s | entry code+0x%X",
             (int)module_length, (const char *)data + 29, exe_u32(data + 110));
    if (!exe_add(info, EXE_HEADER, 0, header_end, label)) return 0;
    if (code_size) {
        snprintf(label, sizeof(label), "Code image: %zu bytes; entry file 0x%zX",
                 code_size, code_offset + exe_u32(data + 110));
        if (!exe_add(info, EXE_REGION, code_offset, code_size, label)) return 0;
    }
    if (data_size && !exe_add(info, EXE_REGION, data_offset, data_size, "Initialized data image")) return 0;

    nlmSpan ranges[8] = {{code_offset, code_size}, {data_offset, data_size},
                         {custom_offset, custom_size}};
    if (!nlm_names(data, size, exe_u32(data + 70), exe_u32(data + 74), info, ranges + 3) ||
        !nlm_imports(data, size, exe_u32(data + 86), exe_u32(data + 90),
                     code_size, data_size, info, ranges + 4) ||
        !nlm_symbols(data, size, exe_u32(data + 94), exe_u32(data + 98),
                     code_size, data_size, bss_size, 0, info, ranges + 5) ||
        !nlm_symbols(data, size, exe_u32(data + 102), exe_u32(data + 106),
                     code_size, data_size, bss_size, 1, info, ranges + 6)) return 0;
    size_t fixups = exe_u32(data + 82), fixup_offset = exe_u32(data + 78);
    if (!nlm_range(size, fixup_offset, fixups) || fixups > (size - fixup_offset) / 4)
        return nlm_bad(info);
    if (!nlm_count(info, fixups)) return 0;
    ranges[7] = (nlmSpan){fixup_offset, fixups * 4};
    for (size_t i = 0; i < fixups; ++i)
        if (!nlm_fixup(exe_u32(data + fixup_offset + i * 4), code_size, data_size))
            return nlm_bad(info);
    for (size_t i = 0; i < 8; ++i) {
        if (ranges[i].length && ranges[i].start < header_end) return nlm_bad(info);
        for (size_t j = i + 1; j < 8; ++j)
            if (ranges[i].length && ranges[j].length &&
                ranges[i].start < ranges[j].start + ranges[j].length &&
                ranges[j].start < ranges[i].start + ranges[i].length) return nlm_bad(info);
    }
    snprintf(info->message, sizeof(info->message), "Primary i386 NLM image; import names require equal-length replacements");
    return 1;
}
