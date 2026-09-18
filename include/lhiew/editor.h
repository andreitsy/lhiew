#pragma once

#include <stddef.h>

void   init_editor(void);
void   switch_mode(void);
size_t editor_offset_width(void);
/* Physical terminal dimensions, including the two status/help rows. */
void   editor_resize(size_t rows, size_t cols);
/* Return nonzero after applying a changed terminal size. */
int    editor_update_window_size(void);
