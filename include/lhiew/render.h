#pragma once

#include "lhiew/append_buffer.h"
#include <stddef.h>

size_t get_byte_position(void);
void   draw_row_text(size_t y, append_buffer *ab);
void   draw_row_hex(size_t y, append_buffer *ab);
void   draw_row_disassembler(size_t y, append_buffer *ab);
void   editor_draw_rows(append_buffer *ab);
void   editor_draw_status_bar(append_buffer *ab);
void   editor_draw_message_bar(append_buffer *ab);
void   editor_scroll(void);
void   editor_refresh_screen(void);
void   editor_set_status_message(const char *fmt, ...);
