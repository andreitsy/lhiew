#pragma once

#include "lhiew/append_buffer.h"
#include <stddef.h>

/* Draw a three-line input prompt, scrolling the input tail to fit. */
void   editor_draw_prompt(append_buffer *ab, const char *title, const char *input,
                          size_t length, const char *help);
void   draw_row_text(size_t y, append_buffer *ab);
void   draw_row_hex(size_t y, append_buffer *ab);
void   draw_row_disassembler(size_t y, append_buffer *ab);
void   editor_draw_rows(append_buffer *ab);
void   editor_draw_status_bar(append_buffer *ab);
void   editor_draw_message_bar(append_buffer *ab);
void   editor_scroll(void);
void   editor_draw_screen(append_buffer *ab);
void   editor_refresh_screen(void);
#if defined(__GNUC__) || defined(__clang__)
__attribute__((format(printf, 1, 2)))
#endif
void   editor_set_status_message(const char *fmt, ...);
