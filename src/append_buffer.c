#include "lhiew/append_buffer.h"

#include <stdlib.h>
#include <string.h>

void append_to_buffer(append_buffer *ab, const char *s, size_t len) {
    char *new = realloc(ab->buffer, ab->len + len);
    if (new == NULL)
        return;
    memcpy(&new[ab->len], s, len);
    ab->buffer = new;
    ab->len += len;
}

void free_append_buffer(append_buffer *ab) {
    free(ab->buffer);
    ab->buffer = NULL;
    ab->len = 0;
}
