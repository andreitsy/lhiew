#include "lhiew/append_buffer.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

void append_to_buffer(append_buffer *ab, const char *s, size_t len) {
    if (!len || len > SIZE_MAX - ab->len)
        return;
    size_t needed = ab->len + len;
    if (needed > ab->capacity) {
        size_t capacity = ab->capacity ? ab->capacity : 256;
        while (capacity < needed) {
            if (capacity > SIZE_MAX / 2) {
                capacity = needed;
                break;
            }
            capacity *= 2;
        }
        char *buffer = realloc(ab->buffer, capacity);
        if (!buffer)
            return;
        ab->buffer = buffer;
        ab->capacity = capacity;
    }
    memcpy(ab->buffer + ab->len, s, len);
    ab->len = needed;
}

void free_append_buffer(append_buffer *ab) {
    free(ab->buffer);
    *ab = (append_buffer)ABUF_INIT;
}
