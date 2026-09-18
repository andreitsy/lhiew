#include "lhiew/types.h"
#include "lhiew/editor.h"
#include "lhiew/disassembler.h"
#include "lhiew/terminal.h"

#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

size_t editor_offset_width(void) {
    size_t digits = 1;
    for (size_t offset = global_cfg.num_bytes; offset >= 16; offset /= 16)
        digits++;
    return digits < 8 ? 8 : digits;
}

void switch_mode(void) {
    size_t old_width = global_cfg.cur_screencols;
    size_t width = global_cfg.screencols;
    if (global_cfg.mode == HEX_MODE) {
        /* Offset, two spaces, hex triplets, " |", ASCII, and closing '|'. */
        size_t overhead = editor_offset_width() + 5;
        width = width > overhead ? (width - overhead) / 4 : 1;
        if (width >= 4)
            width -= width % 4;
    }
    global_cfg.cur_screencols = width ? width : 1;
    if (global_cfg.cur_byte > global_cfg.num_bytes)
        global_cfg.cur_byte = global_cfg.num_bytes;
    if (old_width && old_width != global_cfg.cur_screencols) {
        size_t first_byte = global_cfg.rowoff > global_cfg.num_bytes / old_width
            ? global_cfg.num_bytes : global_cfg.rowoff * old_width;
        global_cfg.rowoff = first_byte / global_cfg.cur_screencols;
    }
    global_cfg.cy = global_cfg.cur_byte / global_cfg.cur_screencols;
    global_cfg.cx = global_cfg.cur_byte % global_cfg.cur_screencols;
    size_t last_row = global_cfg.num_bytes / global_cfg.cur_screencols;
    global_cfg.numrows = global_cfg.num_bytes ? last_row + (last_row < SIZE_MAX) : 0;
}

void editor_resize(size_t rows, size_t cols) {
    size_t content_rows = rows > 2 ? rows - 2 : 0;
    if (content_rows != global_cfg.screenrows ||
        (content_rows && !global_cfg.disassembler_buffer)) {
        if (content_rows) {
            size_t old_rows = global_cfg.disassembler_buffer ? global_cfg.screenrows : 0;
            disassemblerRow *buffer = realloc(global_cfg.disassembler_buffer,
                                              content_rows * sizeof(*buffer));
            if (!buffer)
                die_safely("resize disassembler buffer");
            if (content_rows > old_rows) {
                memset(buffer + old_rows, 0,
                       (content_rows - old_rows) * sizeof(*buffer));
            }
            global_cfg.disassembler_buffer = buffer;
        } else {
            free_disassembler_buffer();
            global_cfg.disassembler_buffer = NULL;
        }
    }
    global_cfg.terminal_rows = rows;
    global_cfg.screenrows = content_rows;
    global_cfg.screencols = cols;
    global_cfg.window_too_small = cols < SCREENCOLS_MIN || rows < SCREENROWS_MIN;
    switch_mode();
}

int editor_update_window_size(void) {
    /* Poll ioctl only: cursor-position queries can consume ordinary keypresses. */
    struct winsize ws;
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == -1 &&
        ioctl(STDIN_FILENO, TIOCGWINSZ, &ws) == -1)
        return 0;
    if (ws.ws_row == global_cfg.terminal_rows && ws.ws_col == global_cfg.screencols)
        return 0;
    editor_resize(ws.ws_row, ws.ws_col);
    return 1;
}

void init_editor(void) {
    /* Raw mode was enabled before initialization; retain its restoration data. */
    global_cfg = (editorConfig){
        .disassembler_mode = MODE_LONG_COMPAT_32,
        .architecture = ARCH_X86,
        .mode = TEXT_MODE,
        .executable_imports = 1,
        .cur_screencols = 1,
        .orig_termios = global_cfg.orig_termios,
    };

    size_t rows, cols;
    if (get_window_size(&rows, &cols) == -1)
        die_safely("getWindowSize");
    editor_resize(rows, cols);
    atexit(free_disassembler_buffer);
}
