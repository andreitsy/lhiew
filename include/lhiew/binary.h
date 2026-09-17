#pragma once

#include "lhiew/types.h"

typedef enum binaryDetectStatus {
    BINARY_RAW = 0,
    BINARY_DETECTED,
    BINARY_UNSUPPORTED,
    BINARY_MALFORMED,
} binaryDetectStatus;

typedef enum binaryFormat {
    BINARY_FORMAT_RAW = 0,
    BINARY_FORMAT_ELF,
    BINARY_FORMAT_PE,
    BINARY_FORMAT_TE,
    BINARY_FORMAT_MACHO,
    BINARY_FORMAT_MACHO_FAT,
    BINARY_FORMAT_MZ,
    BINARY_FORMAT_NE,
    BINARY_FORMAT_LE,
    BINARY_FORMAT_LX,
    BINARY_FORMAT_NLM,
} binaryFormat;

typedef struct binaryRegion {
    size_t offset;
    size_t size;
    uint64_t address;
    int executable;
} binaryRegion;

typedef struct binaryInfo {
    architectureSpec architecture;
    binaryDetectStatus status;
    binaryFormat format;
    int has_entry;
    size_t entry_offset;
    /* Validated container metadata; instruction byte order can differ. */
    int container_big_endian;
    int container_64;
    size_t table_offset;
    size_t table_count;
    size_t table_stride;
    size_t secondary_offset;
    size_t secondary_count;
    size_t secondary_stride;
    uint64_t image_base;
    size_t stripped_size;
    size_t data_offset;
    size_t data_size;
} binaryInfo;

void binary_detect(const uint8_t *data, size_t size, binaryInfo *info);
int binary_region_at(const uint8_t *data, size_t size, const binaryInfo *info,
                     size_t offset, binaryRegion *region);
const char *binary_format_name(binaryFormat format);
