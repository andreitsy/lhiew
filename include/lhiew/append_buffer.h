#pragma once

#include <stddef.h>

typedef struct append_buffer {
    char  *buffer;
    size_t len;
} append_buffer;

#define ABUF_INIT {NULL, 0}

void append_to_buffer(append_buffer *ab, const char *s, size_t len);
void free_append_buffer(append_buffer *ab);
