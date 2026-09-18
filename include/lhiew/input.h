#pragma once

#include <stddef.h>

#define CTRL_KEY(k) ((k) & 0x1f)

int editor_hex_digit(int key);
/* Capacity limits new input, including the NUL. Retained text may exceed this
   limit after switching prompt modes; the buffer must still cover *length + 1.
   Return -1 for a full input, zero for ignored keys, one for edits. */
int editor_prompt_key(char *text, size_t *length, size_t capacity, int key);
size_t editor_menu_choice(size_t choice, size_t count, int key);
void editor_move_cursor(int key);
void editor_process_keypress(void);
