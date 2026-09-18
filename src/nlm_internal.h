#pragma once

#include "byte_reader.h"

#include <string.h>

#define NLM_HEADER_SIZE 130
#define NLM_SIGNATURE "NetWare Loadable Module\x1a"

/* Call after checking the i386 v4 signature and version. The fixed module
   name is followed by three length-prefixed, NUL-terminated header strings. */
static inline int nlm_header_end(const uint8_t *data, size_t size, size_t *end) {
    if (size < NLM_HEADER_SIZE || !data[28] || data[28] > 12 ||
        data[29 + data[28]] || memchr(data + 29, 0, data[28])) return 0;
    size_t cursor = NLM_HEADER_SIZE;
    const size_t maximum[] = {127, 71, 17};
    for (size_t i = 0; i < sizeof(maximum) / sizeof(maximum[0]); ++i) {
        if (!byte_span(size, cursor, 1)) return 0;
        size_t length = data[cursor++];
        if (length > maximum[i] || !byte_span(size, cursor, length + 1) ||
            data[cursor + length]) return 0;
        cursor += length + 1;
        if (i == 0) { /* Stack size, reserved word and oldThreadName[6]. */
            if (!byte_span(size, cursor, 14)) return 0;
            cursor += 14;
        }
    }
    *end = cursor;
    return 1;
}
