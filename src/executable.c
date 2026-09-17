#include "lhiew/types.h"
#include "lhiew/executable.h"
#include "executable_internal.h"

#include <string.h>

int exe_error(executableInfo *info, executableStatus status, const char *message) {
    info->status = status;
    snprintf(info->message, sizeof(info->message), "%s", message);
    return 0;
}

executableRow *exe_add(executableInfo *info, executableRowKind kind,
                       size_t offset, size_t length, const char *label) {
    if (info->count == 100000) {
        exe_error(info, EXE_LIMIT, "Executable browser row limit reached (100000)");
        return NULL;
    }
    if (info->count == info->capacity) {
        size_t capacity = info->capacity ? info->capacity * 2 : 32;
        executableRow *rows = realloc(info->rows, capacity * sizeof(*rows));
        if (!rows) {
            exe_error(info, EXE_LIMIT, "Not enough memory for executable tables");
            return NULL;
        }
        info->rows = rows;
        info->capacity = capacity;
    }
    executableRow *row = &info->rows[info->count++];
    memset(row, 0, sizeof(*row));
    row->kind = kind;
    row->offset = offset;
    row->length = length;
    row->name_offset = row->ordinal_offset = row->mirror_offset = SIZE_MAX;
    snprintf(row->label, sizeof(row->label), "%s", label);
    return row;
}

int exe_name(const uint8_t *data, size_t size, size_t offset, size_t end,
             int pascal, char out[256], size_t *chars, size_t *length) {
    if (end > size || offset >= end)
        return 0;
    size_t start = offset, len;
    if (pascal) {
        len = data[offset];
        start++;
        if (!exe_span(end, start, len)) return 0;
        if (memchr(data + start, 0, len)) return 0;
    } else {
        len = 0;
        while (len < 256 && len < end - start && data[start + len])
            len++;
        if (len == 256 || len == end - start) return 0;
    }
    memcpy(out, data + start, len);
    out[len] = '\0';
    *chars = start;
    *length = len;
    return 1;
}

void executable_free(executableInfo *info) {
    free(info->rows);
    memset(info, 0, sizeof(*info));
}

void executable_parse(const uint8_t *data, size_t size, executableInfo *info) {
    executable_free(info);
    snprintf(info->format, sizeof(info->format), "Unknown");
    if (!data || !size) {
        exe_error(info, EXE_UNSUPPORTED, "No executable metadata in this file");
        return;
    }
    if (size >= 8 && !memcmp(data, "NetWare ", 8)) {
        exe_parse_nlm(data, size, info);
        goto finished;
    }
    size_t header = 0;
    if (size >= 2 && !memcmp(data, "MZ", 2)) {
        if (size < 64) {
            exe_error(info, EXE_MALFORMED, "Truncated DOS header");
            return;
        }
        header = exe_u32(data + 60);
        if (header < 64 || !exe_span(size, header, 2)) {
            exe_error(info, EXE_UNSUPPORTED, "No PE/NE/LE/LX header in DOS file");
            return;
        }
    }
    if (exe_span(size, header, 4) && !memcmp(data + header, "PE\0\0", 4)) {
        exe_parse_pe(data, size, header, info);
    } else if (exe_span(size, header, 2) && !memcmp(data + header, "NE", 2)) {
        exe_parse_ne(data, size, header, info);
    } else if (exe_span(size, header, 2) &&
               (!memcmp(data + header, "LE", 2) || !memcmp(data + header, "LX", 2))) {
        exe_parse_linear(data, size, header, info);
    } else {
        exe_error(info, EXE_UNSUPPORTED, "Browser supports PE, NE, LE, LX and i386 NLM");
    }
finished:
    if (info->status != EXE_OK) {
        for (size_t i = 0; i < info->count; ++i)
            info->rows[i].editable = 0;
    }
}
