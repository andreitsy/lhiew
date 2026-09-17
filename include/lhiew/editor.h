#pragma once

#include <stddef.h>

#define TAB_STOP 4

void   init_editor(void);
void   switch_mode(void);
size_t get_row_len(void);
size_t editor_offset_width(void);
/* Physical terminal dimensions, including the two status/help rows. */
void   editor_resize(size_t rows, size_t cols);
/* Return nonzero after applying a changed terminal size. */
int    editor_update_window_size(void);
