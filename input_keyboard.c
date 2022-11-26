#include "input_keyboard.h"

void editor_move_cursor(int key) {
    editorRow row;
    if (global_cfg.cy >= global_cfg.numrows) {
        row.size = 0;
        row.chars = NULL;
    } else {
        row.size = get_row_len();
        row.chars = (char *) &global_cfg.file[global_cfg.cy * global_cfg.cur_screencols];
    }

    if (global_cfg.mode == DISSASEMBLER_MODE) {
        size_t cur_row;
        for(cur_row = 0; cur_row < global_cfg.screenrows;++cur_row) {
            if (global_cfg.dissasembler_buffer[cur_row].start_byte <= global_cfg.cur_byte
                && global_cfg.cur_byte < global_cfg.dissasembler_buffer[cur_row].end_byte) {
                break;
            }
        }
        size_t shift = global_cfg.cur_byte - global_cfg.dissasembler_buffer[cur_row].start_byte;
        switch (key) {
            case ARROW_LEFT:
            case 'h':
                if (global_cfg.cur_byte)
                    global_cfg.cur_byte--;
                break;
            case ARROW_RIGHT:
            case 'l':
                if (global_cfg.cur_byte < global_cfg.num_bytes)
                    global_cfg.cur_byte++;
                break;
            case ARROW_UP:
            case 'k':
                if (global_cfg.cur_byte > shift + 1)
                    global_cfg.cur_byte -= (shift + 1);
                break;
            case ARROW_DOWN:
            case 'j':
                if (cur_row < global_cfg.screenrows - 1) {
                    size_t tmp_start = global_cfg.dissasembler_buffer[cur_row+1].start_byte;
                    size_t tmp_end = global_cfg.dissasembler_buffer[cur_row+1].end_byte;
                    if (tmp_start + shift < tmp_end) {
                        global_cfg.cur_byte = tmp_start + shift;
                    } else {
                        global_cfg.cur_byte = tmp_end - 1;
                    }
                }
                break;
        }
        size_t rowlen = get_row_len();
        if (global_cfg.cx > rowlen) {
            global_cfg.cx = rowlen - 1;
        }
        global_cfg.cy = global_cfg.cur_byte / global_cfg.cur_screencols;
        global_cfg.cx = global_cfg.cur_byte % global_cfg.cur_screencols;
    } else {
        switch (key) {
            case ARROW_LEFT:
            case 'h':
                if (global_cfg.cx > 0) {
                    global_cfg.cx--;
                } else if (global_cfg.cy > 0) {
                    global_cfg.cy--;
                    global_cfg.cx = row.size - 1;
                }
                break;
            case ARROW_RIGHT:
            case 'l':
                if (row.chars && global_cfg.cx < row.size - 1) {
                    global_cfg.cx++;
                } else if (row.chars && global_cfg.cx == row.size - 1) {
                    global_cfg.cy++;
                    global_cfg.cx = 0;
                }
                break;
            case ARROW_UP:
            case 'k':
                if (global_cfg.cy != 0) {
                    global_cfg.cy--;
                }
                break;
            case ARROW_DOWN:
            case 'j':
                if (global_cfg.cy < global_cfg.numrows) {
                    global_cfg.cy++;
                }
                break;
        }
        size_t rowlen = get_row_len();
        if (global_cfg.cx > rowlen) {
            global_cfg.cx = rowlen - 1;
        }
        global_cfg.cur_byte = global_cfg.cy * global_cfg.cur_screencols + global_cfg.cx;
    }

}

void editor_process_keypress(void) {
    int c = editor_read_key();
    switch (c) {
        case CTRL_KEY('m'):
            global_cfg.mode = global_cfg.mode == 0 ? DISSASEMBLER_MODE : global_cfg.mode - 1;
            switch_mode();
            editor_refresh_screen();
            break;
        case 'm':
            global_cfg.mode = (global_cfg.mode + 1) % 3;
            switch_mode();
            editor_refresh_screen();
            break;
        case 'o':
            global_cfg.dissasembler_mode = (global_cfg.dissasembler_mode + 1) % 4;
            editor_refresh_screen();
            break;
        case CTRL_KEY('q'):
            write(STDOUT_FILENO, "\x1b[2J", 4);
            write(STDOUT_FILENO, "\x1b[H", 3);
            exit(0);
            break;
        case PAGE_UP:
        case PAGE_DOWN: {
            if (c == PAGE_UP) {
                global_cfg.cy = global_cfg.rowoff;
            } else { // c == PAGE_DOWN
                global_cfg.cy = global_cfg.rowoff + global_cfg.screenrows - 1;
                if (global_cfg.cy > global_cfg.numrows)
                    global_cfg.cy = global_cfg.numrows;
            }

            size_t times = global_cfg.screenrows;
            while (times--)
                editor_move_cursor(c == PAGE_UP ? ARROW_UP : ARROW_DOWN);
        }
            break;
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
