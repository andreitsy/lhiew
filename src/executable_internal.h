#pragma once

#include "lhiew/executable.h"

static inline int exe_span(size_t size, size_t offset, size_t length) {
    return offset <= size && length <= size - offset;
}

static inline uint16_t exe_u16(const uint8_t *p) {
    return (uint16_t)p[0] | (uint16_t)p[1] << 8;
}

static inline uint32_t exe_u32(const uint8_t *p) {
    return (uint32_t)exe_u16(p) | (uint32_t)exe_u16(p + 2) << 16;
}

static inline uint64_t exe_u64(const uint8_t *p) {
    return (uint64_t)exe_u32(p) | (uint64_t)exe_u32(p + 4) << 32;
}

executableRow *exe_add(executableInfo *info, executableRowKind kind,
                       size_t offset, size_t length, const char *label);
int exe_error(executableInfo *info, executableStatus status, const char *message);
/* Copies a bounded name (max 255 bytes), returning its character span. */
int exe_name(const uint8_t *data, size_t size, size_t offset, size_t end,
             int pascal, char out[256], size_t *chars, size_t *length);
int exe_parse_pe(const uint8_t *data, size_t size, size_t header, executableInfo *info);
int exe_parse_ne(const uint8_t *data, size_t size, size_t header, executableInfo *info);
int exe_parse_linear(const uint8_t *data, size_t size, size_t header, executableInfo *info);
int exe_parse_nlm(const uint8_t *data, size_t size, executableInfo *info);
