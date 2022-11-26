#include "output.h"

size_t get_byte_position(void) {
    size_t cur_pos = global_cfg.cx + global_cfg.cur_screencols * global_cfg.cy;
    return cur_pos > global_cfg.num_bytes ? global_cfg.num_bytes : cur_pos;
}

void draw_row_text(size_t y, append_buffer *ab) {
    size_t len = get_row_len();
    size_t filerow = y + global_cfg.rowoff;
    char *c = (char *) &global_cfg.file[filerow * global_cfg.cur_screencols];
    for (size_t j = 0; j < len; j++) {
        if (isprint(c[j])) {
            append_to_buffer(ab, &c[j], 1);
        } else {
            if (iscntrl(c[j])) {
                char sym = (c[j] <= 26) ? '.' : '@';
                append_to_buffer(ab, &sym, 1);
            } else {
                append_to_buffer(ab, "?", 1);
            }
        }
    }
}

void draw_row_hex(size_t y, append_buffer *ab) {
    char *str_tmp = NULL;
    int cursor_pos = 0;
    size_t len = get_row_len();
    size_t filerow = y + global_cfg.rowoff;
    uint8_t *b = &global_cfg.file[filerow * global_cfg.cur_screencols];
    append_to_buffer(ab, "|", 1);
    for (size_t j = 0; j < len; j++) {
        if (global_cfg.cy == filerow && global_cfg.cx == j) {
            cursor_pos = 1;
        } else {
            cursor_pos = 0;
        }
        if (j && j % 4 == 0) {
            append_to_buffer(ab, " ", 1);
        }
        append_to_buffer(ab, " ", 1);
        str_tmp = (char *) malloc(5 * sizeof(char));
        snprintf(str_tmp, 4, "%02x", b[j]);
        if (cursor_pos) {
            append_to_buffer(ab, "\x1b[7m", 4); // highlight current byte
        }
        append_to_buffer(ab, str_tmp, 3);
        if (cursor_pos) {
            append_to_buffer(ab, "\x1b[m", 3); // highlight current byte
        }
        free(str_tmp);
    }
    str_tmp = (char *) malloc(12 * sizeof(char));
    snprintf(str_tmp, 12, " |%08zx0", filerow);
    append_to_buffer(ab, str_tmp, 12);
    free(str_tmp);
}

void draw_row_dissasembler(size_t y, append_buffer *ab) {
    size_t dissasembler_len = strlen(global_cfg.dissasembler_buffer[y].diss_str);
    size_t padding = global_cfg.cur_screencols;
    size_t start = global_cfg.dissasembler_buffer[y].start_byte;
    char buf[12] = "";
    padding -= sprintf(buf, "%09zx| ", start);
    append_to_buffer(ab, buf, 12);
    for (size_t i = start; i < start + 16; i++)
    {
        if (i == global_cfg.cur_byte) {
            append_to_buffer(ab, "\x1b[7m", 4); // highlight current byte
        }
        if (i < global_cfg.dissasembler_buffer[y].end_byte) {
            sprintf(buf, "%02x", global_cfg.file[i]);
            append_to_buffer(ab, buf, 2);
        } else {
            append_to_buffer(ab, "  ", 2);
        }
        if (i == global_cfg.cur_byte) {
            append_to_buffer(ab, "\x1b[m", 3); // highlight current byte
        }
        padding -= 2;
    }
    append_to_buffer(ab, "| ", 2);
    padding -= 2;
    append_to_buffer(ab, global_cfg.dissasembler_buffer[y].diss_str,
                     dissasembler_len);
    padding -= dissasembler_len;
    while (padding--) {
        append_to_buffer(ab, " ", 1);
    }
}

void editor_draw_rows(append_buffer *ab) {
    size_t y;

    switch (global_cfg.mode) {
        case TEXT_MODE:
            for (y = 0; y < global_cfg.screenrows; y++) {
                size_t filerow = y + global_cfg.rowoff;
                if (filerow >= global_cfg.numrows) {
                    if (global_cfg.numrows == 0
                        && y == global_cfg.screenrows / 3) {
                        char welcome[80];
                        size_t welcomelen = snprintf(
                                welcome, sizeof(welcome),
                                "Linux HIEW editor -- version %s",
                                EDITOR_VERSION);
                        if (welcomelen > global_cfg.cur_screencols)
                            welcomelen = global_cfg.cur_screencols;
                        size_t padding = (global_cfg.cur_screencols - welcomelen) / 2;
                        if (padding) {
                            append_to_buffer(ab, "~", 1);
                            padding--;
                        }
                        while (padding--)
                            append_to_buffer(ab, " ", 1);
                        append_to_buffer(ab, welcome, welcomelen);
                    } else {
                        append_to_buffer(ab, "~", 1);
                    }
                } else {
                    draw_row_text(y, ab);
                }
                append_to_buffer(ab, "\x1b[K", 3);
                append_to_buffer(ab, "\r\n", 2);
            }
            break;
        case HEX_MODE:
            for (y = 0; y < global_cfg.screenrows; y++) {
                draw_row_text(y, ab);
                draw_row_hex(y, ab);
                append_to_buffer(ab, "\x1b[K", 3);
                append_to_buffer(ab, "\r\n", 2);
            }
            break;
        case DISSASEMBLER_MODE:
            dissameble_block(global_cfg.cur_byte);
            for (y = 0; y < global_cfg.screenrows; y++) {
                draw_row_dissasembler(y, ab);
                append_to_buffer(ab, "\r\n", 2);
            }
            break;
    }
}

void editor_draw_status_bar(append_buffer *ab) {
    append_to_buffer(ab, "\x1b[7m", 4);
    char status[80], rstatus[80];
    size_t len = snprintf(
            status, sizeof(status), "%.20s",
            global_cfg.filename ? global_cfg.filename : "[ No File is Open ]");
    char *format_str = NULL;
    char *dissambler_mode_str = "";
    switch (global_cfg.mode) {
        case TEXT_MODE:
            format_str = TEXT_MODE_STR;
            break;
        case HEX_MODE:
            format_str = HEX_MODE_STR;
            break;
        case DISSASEMBLER_MODE:
            format_str = DISSASEMBLER_MODE_STR;
            break;
    }
    switch (global_cfg.dissasembler_mode) {
        case REAL:
            dissambler_mode_str = "Real opsize";
            break;
        case MODE_LONG_COMPAT_16:
            dissambler_mode_str = "Long 16 opsize";
            break;
        case MODE_LONG_COMPAT_32:
            dissambler_mode_str = "Long 32 opsize";
            break;
        case MODE_LONG_COMPAT_64:
            dissambler_mode_str = "Long 64 opsize";
            break;
    }
    size_t rlen;
    if (global_cfg.mode == DISSASEMBLER_MODE) {
        rlen = snprintf(rstatus, sizeof(rstatus), "%s (%s) %zu:%zu",
                        format_str,
                        dissambler_mode_str,
                        get_byte_position(),
                        global_cfg.num_bytes);
    } else {
        rlen = snprintf(rstatus, sizeof(rstatus), "%s %zu:%zu - %lu/%zu",
                        format_str,
                        get_byte_position(),
                        global_cfg.num_bytes,
                        global_cfg.cy + 1,
                        global_cfg.numrows);
    }

    if (len > global_cfg.screencols)
        len = global_cfg.screencols;
    append_to_buffer(ab, status, len);
    while (len < global_cfg.screencols) {
        if (global_cfg.screencols - len == rlen) {
            append_to_buffer(ab, rstatus, rlen);
            break;
        } else {
            append_to_buffer(ab, " ", 1);
            len++;
        }
    }
    append_to_buffer(ab, "\x1b[m", 3);
    append_to_buffer(ab, "\r\n", 2);
}

void editor_draw_message_bar(append_buffer *ab) {
    append_to_buffer(ab, "\x1b[K", 3);
    size_t msg_len = strlen(global_cfg.statusmsg);
    if (msg_len > global_cfg.screencols)
        msg_len = global_cfg.screencols;
    if (msg_len && time(NULL) - global_cfg.statusmsg_time < 5)
        append_to_buffer(ab, global_cfg.statusmsg, msg_len);
}

void editor_scroll(void) {
    /*
     * check if the cursor has moved outside the visible window,
     * and if so, adjust global_cfg.rowoff so that the cursor is just inside
     * the visible window.
     * We’ll put this logic in a function called editorScroll(),
     * and call it right before we refresh the screen.
     */
    global_cfg.rx = 0;
    if (global_cfg.cy < global_cfg.numrows) {
        global_cfg.rx = global_cfg.cx;
    }
    if (global_cfg.cy < global_cfg.rowoff) {
        global_cfg.rowoff = global_cfg.cy;
    }
    if (global_cfg.cy >= global_cfg.rowoff + global_cfg.screenrows) {
        global_cfg.rowoff = global_cfg.cy - global_cfg.screenrows + 1;
    }
    if (global_cfg.rx < global_cfg.coloff) {
        global_cfg.coloff = global_cfg.rx;
    }
    if (global_cfg.rx >= global_cfg.coloff + global_cfg.cur_screencols) {
        global_cfg.coloff = global_cfg.rx - global_cfg.cur_screencols + 1;
    }
}

void editor_refresh_screen(void) {
    editor_scroll();
    append_buffer ab = ABUF_INIT;
    append_to_buffer(&ab, "\x1b[?25l", 6); // hide cursor to avoid blinking
    append_to_buffer(&ab, "\x1b[H", 3); // set cursor to begin of the line
    editor_draw_rows(&ab);
    editor_draw_status_bar(&ab);
    editor_draw_message_bar(&ab);

    char buf[32];
    snprintf(buf, sizeof(buf), "\x1b[%lu;%luH",
             (global_cfg.cy - global_cfg.rowoff) + 1,
             (global_cfg.rx - global_cfg.coloff) + 1);
    append_to_buffer(&ab, buf, strlen(buf));
    if (global_cfg.mode != DISSASEMBLER_MODE) {
        append_to_buffer(&ab, "\x1b[?25h", 6); // make cursor visible
    }
    write(STDOUT_FILENO, ab.buffer, ab.len);
    free_append_buffer(&ab);
}

void editor_set_status_message(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(global_cfg.statusmsg, sizeof(global_cfg.statusmsg), fmt, ap);
    va_end(ap);
    global_cfg.statusmsg_time = time(NULL);
}
