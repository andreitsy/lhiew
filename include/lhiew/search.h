#pragma once

#include "lhiew/append_buffer.h"
#include "lhiew/types.h"

/* Translate typed prompt text into pattern bytes. Hexadecimal input accepts
   spaces or tabs between complete byte pairs and needs complete pairs; text input is literal.
   Returns nonzero and sets *length on success, leaving *length zero for empty
   or invalid input. Writes at most SEARCH_PATTERN_MAX bytes. */
int search_compile(const char *text, size_t text_length, int ascii,
                   uint8_t *pattern, size_t *length);
/* Scan for a match starting at or after from (forward), or at or before from
   (backward). Returns nonzero and sets *found on a match. */
int search_find(const uint8_t *data, size_t size, const uint8_t *pattern,
                size_t length, size_t from, int backward, size_t *found);

void search_open_prompt(void);
void search_keypress(int key);
/* Repeat the stored pattern from the cursor. Nonzero when the cursor moved. */
int  search_repeat(int backward);
void search_draw_prompt(append_buffer *ab);
const char *search_prompt_message(void);
