#include "lhiew/types.h"
#include "lhiew/input.h"
#include "lhiew/disassembler.h"
#include "lhiew/editor.h"
#include "lhiew/render.h"
#include "lhiew/terminal.h"

#include <stdlib.h>
#include <unistd.h>

void editor_move_cursor(int key) {
    if (global_cfg.window_too_small || !global_cfg.num_bytes)
        return;

    size_t position = global_cfg.cur_byte;
    size_t width = global_cfg.cur_screencols;
    if (global_cfg.mode == DISASSEMBLER_MODE) {
        disassemble_block(position);
        size_t row = 0;
        while (row < global_cfg.screenrows &&
               !(global_cfg.disassembler_buffer[row].start_byte <= position &&
                 position < global_cfg.disassembler_buffer[row].end_byte))
            row++;
        size_t start = row < global_cfg.screenrows
            ? global_cfg.disassembler_buffer[row].start_byte : position;
        size_t shift = position - start;
        switch (key) {
            case ARROW_LEFT:
            case 'h':
                if (position) position--;
                break;
            case ARROW_RIGHT:
            case 'l':
                if (position < global_cfg.num_bytes) position++;
                break;
            case ARROW_UP:
            case 'k':
                position = start ? start - 1 : 0;
                break;
            case ARROW_DOWN:
            case 'j':
                if (row < global_cfg.screenrows) {
                    size_t end = global_cfg.disassembler_buffer[row].end_byte;
                    position = end;
                    if (row + 1 < global_cfg.screenrows) {
                        disassemblerRow *next = &global_cfg.disassembler_buffer[row + 1];
                        if (next->end_byte > next->start_byte) {
                            size_t len = next->end_byte - next->start_byte;
                            position = next->start_byte + (shift < len ? shift : len - 1);
                        }
                    }
                }
                break;
        }
    } else {
        switch (key) {
            case ARROW_LEFT:
            case 'h':
                if (position) position--;
                break;
            case ARROW_RIGHT:
            case 'l':
                if (position < global_cfg.num_bytes) position++;
                break;
            case ARROW_UP:
            case 'k':
                if (position >= width) position -= width;
                break;
            case ARROW_DOWN:
            case 'j':
                position += global_cfg.num_bytes - position < width
                    ? global_cfg.num_bytes - position : width;
                break;
        }
    }
    global_cfg.cur_byte = position;
    switch_mode();
}

void editor_process_keypress(void) {
    int c = editor_read_key();
    if (c == CTRL_KEY('q')) {
        write(STDOUT_FILENO, "\x1b[2J\x1b[H", 7);
        exit(0);
    }
    if (editor_update_window_size())
        editor_refresh_screen();
    if (!c || global_cfg.window_too_small)
        return;
    switch (c) {
        case CTRL_KEY('m'):
            global_cfg.mode = global_cfg.mode == 0 ? DISASSEMBLER_MODE : global_cfg.mode - 1;
            switch_mode();
            break;
        case 'm':
            global_cfg.mode = (global_cfg.mode + 1) % 3;
            switch_mode();
            break;
        case 'o':
            global_cfg.disassembler_mode = (global_cfg.disassembler_mode + 1) % 4;
            break;
        case PAGE_UP:
        case PAGE_DOWN: {
            if (global_cfg.mode == DISASSEMBLER_MODE)
                break;
            size_t distance = global_cfg.screenrows * global_cfg.cur_screencols;
            if (c == PAGE_UP) {
                global_cfg.cur_byte -= global_cfg.cur_byte < distance
                    ? global_cfg.cur_byte : distance;
            } else {
                size_t remaining = global_cfg.num_bytes - global_cfg.cur_byte;
                global_cfg.cur_byte += remaining < distance ? remaining : distance;
            }
            switch_mode();
            break;
        }
        case ARROW_UP:
        case ARROW_DOWN:
        case ARROW_LEFT:
        case ARROW_RIGHT:
        case 'h':
        case 'l':
        case 'k':
        case 'j':
            editor_move_cursor(c);
            break;
    }
}
