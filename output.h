#ifndef OTUS_PROJECT_OUTPUT_H
#define OTUS_PROJECT_OUTPUT_H
#include "data.h"
#include "editor.h"
#include <stdarg.h>
#include "dissasembler.h"

size_t get_byte_position(void);
void draw_row_text(size_t y, append_buffer *ab);
void draw_row_hex(size_t y, append_buffer *ab);
void draw_row_dissasembler(size_t y, append_buffer *ab);
void editor_draw_rows(struct append_buffer *ab);
void editor_draw_status_bar(append_buffer *ab);
void editor_draw_message_bar(append_buffer *ab);
void editor_scroll(void);
void editor_refresh_screen(void);
void editor_set_status_message(const char *fmt, ...);

#endif //OTUS_PROJECT_OUTPUT_H
