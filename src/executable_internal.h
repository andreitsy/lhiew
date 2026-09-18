#pragma once

#include "lhiew/executable.h"
#include "byte_reader.h"

#if defined(__GNUC__) || defined(__clang__)
__attribute__((format(printf, 5, 6)))
#endif
executableRow *exe_add(executableInfo *info, executableRowKind kind,
                       size_t offset, size_t length, const char *format, ...);
int exe_error(executableInfo *info, executableStatus status, const char *message);
/* Copies a bounded name (max 255 bytes), returning its character span. */
int exe_name(const uint8_t *data, size_t size, size_t offset, size_t end,
             int pascal, char out[256], size_t *chars, size_t *length);
int exe_parse_pe(const uint8_t *data, size_t size, size_t header, executableInfo *info);
int exe_parse_ne(const uint8_t *data, size_t size, size_t header, executableInfo *info);
int exe_parse_linear(const uint8_t *data, size_t size, size_t header, executableInfo *info);
int exe_parse_nlm(const uint8_t *data, size_t size, executableInfo *info);
