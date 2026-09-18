#pragma once

#include <stddef.h>

enum editorKey {
    ARROW_LEFT = 1001,
    ARROW_RIGHT,
    ARROW_UP,
    ARROW_DOWN,
    PAGE_UP,
    PAGE_DOWN,
    DEL_KEY,
    SHIFT_F1,
    F3_KEY,
    F5_KEY,
    F7_KEY,
    SHIFT_F7,
    F8_KEY,
    F9_KEY,
    F10_KEY,
    HOME_KEY,
    END_KEY,
    CTRL_HOME,
    CTRL_END,
};

_Noreturn void die_safely(const char *s);
void enable_raw_mode(void);
void disable_raw_mode(void);
int  editor_read_key(void);
int  get_cursor_position(size_t *rows, size_t *cols);
int  get_window_size(size_t *rows, size_t *cols);
