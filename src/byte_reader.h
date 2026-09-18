#pragma once

#include <stddef.h>
#include <stdint.h>

/* Validate before reading. Subtraction keeps untrusted extents from wrapping. */
static inline int byte_span(size_t size, uint64_t offset, uint64_t length) {
    return offset <= size && length <= size - (size_t)offset;
}

static inline int byte_overlap(uint64_t a, uint64_t a_length,
                                uint64_t b, uint64_t b_length) {
    return a_length && b_length &&
           (a <= b ? b - a < a_length : a - b < b_length);
}

/* Serialized integers need neither aligned pointers nor host byte order. */
static inline uint16_t read_u16(const uint8_t *p, int be) {
    return be ? (uint16_t)((uint16_t)p[0] << 8 | p[1])
              : (uint16_t)((uint16_t)p[1] << 8 | p[0]);
}

static inline uint32_t read_u32(const uint8_t *p, int be) {
    return be ? (uint32_t)read_u16(p, be) << 16 | read_u16(p + 2, be)
              : (uint32_t)read_u16(p + 2, be) << 16 | read_u16(p, be);
}

static inline uint64_t read_u64(const uint8_t *p, int be) {
    return be ? (uint64_t)read_u32(p, be) << 32 | read_u32(p + 4, be)
              : (uint64_t)read_u32(p + 4, be) << 32 | read_u32(p, be);
}

static inline uint16_t read_le16(const uint8_t *p) { return read_u16(p, 0); }
static inline uint32_t read_le32(const uint8_t *p) { return read_u32(p, 0); }
static inline uint64_t read_le64(const uint8_t *p) { return read_u64(p, 0); }
