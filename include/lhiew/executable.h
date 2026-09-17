#pragma once

#include "lhiew/types.h"

typedef enum executableStatus {
    EXE_OK = 0, EXE_UNSUPPORTED, EXE_MALFORMED, EXE_LIMIT
} executableStatus;

typedef enum executableRowKind {
    EXE_HEADER = 0, EXE_REGION, EXE_MODULE, EXE_IMPORT
} executableRowKind;

typedef struct executableRow {
    executableRowKind kind;
    char label[576];
    size_t offset;               /* Jump to the underlying record/data. */
    size_t length;
    size_t name_offset;          /* SIZE_MAX if not a name field. */
    size_t name_length;          /* Edits preserve this exact byte length. */
    size_t ordinal_offset;       /* SIZE_MAX if not an ordinal field. */
    unsigned ordinal_width;     /* Existing field width: 1, 2, 4 or 8. */
    uint32_t ordinal;
    uint32_t ordinal_max;
    uint64_t ordinal_flags;      /* Preserved encoding bits, e.g. PE high bit. */
    size_t mirror_offset;        /* Optional unbound PE IAT ordinal field. */
    int editable;
} executableRow;

typedef struct executableInfo {
    char format[24];
    executableStatus status;
    char message[160];
    executableRow *rows;
    size_t count;
    size_t capacity;
} executableInfo;

/* Initialize with {0}; parse/free own the dynamically allocated row list. */
void executable_parse(const uint8_t *data, size_t size, executableInfo *info);
void executable_free(executableInfo *info);
