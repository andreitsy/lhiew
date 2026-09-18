#include "lhiew/types.h"
#include "lhiew/render.h"
#include "lhiew/architecture.h"
#include "lhiew/disassembler.h"
#include "lhiew/editor.h"
#include "lhiew/executable_browser.h"
#include "lhiew/hex_edit.h"
#include "lhiew/search.h"
#include "lhiew/terminal.h"

#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

static void append_spaces(append_buffer *ab, size_t count) {
    static const char spaces[] = "                                                                ";
    while (count) {
        size_t len = count < sizeof(spaces) - 1 ? count : sizeof(spaces) - 1;
        append_to_buffer(ab, spaces, len);
        count -= len;
    }
}

/* Render one terminal cell per byte, regardless of locale or file contents. */
static char printable_byte(uint8_t byte) {
    if (byte >= 32 && byte < 127)
        return (char)byte;
    if (byte < 32 || byte == 127)
        return byte <= 26 ? '.' : '@';
    return '?';
}

static void append_clipped(append_buffer *ab, const char *text, size_t len,
                           size_t width, int mark_truncation) {
    size_t count = len < width ? len : width;
    for (size_t i = 0; i < count; ++i) {
        char byte = mark_truncation && len > width && i + 1 == width
            ? '~' : printable_byte((uint8_t)text[i]);
        append_to_buffer(ab, &byte, 1);
    }
}

static void append_position(append_buffer *ab, size_t row, size_t col) {
    char position[64];
    int len = snprintf(position, sizeof(position), "\x1b[%zu;%zuH", row, col);
    append_to_buffer(ab, position, (size_t)len);
}

static void append_selected(append_buffer *ab, const char *text, size_t length, int selected) {
    if (selected) append_to_buffer(ab, "\x1b[7m", 4);
    append_to_buffer(ab, text, length);
    if (selected) append_to_buffer(ab, "\x1b[m", 3);
}

static void append_hex_byte(append_buffer *ab, size_t position) {
    static const char digits[] = "0123456789abcdef";
    uint8_t byte = global_cfg.file[position];
    char hex[] = {digits[byte >> 4], digits[byte & 15]};
    append_selected(ab, hex, sizeof(hex), position == global_cfg.cur_byte);
}

static void append_offset(append_buffer *ab, size_t offset, int selected) {
    char text[2 * sizeof(size_t) + 1];
    int length = snprintf(text, sizeof(text), "%0*zx", (int)editor_offset_width(), offset);
    append_selected(ab, text, (size_t)length, selected);
    append_to_buffer(ab, "  ", 2);
}

static size_t displayed_row_length(size_t y, size_t *start) {
    size_t columns = global_cfg.cur_screencols;
    if (!columns || !global_cfg.file || y > SIZE_MAX - global_cfg.rowoff)
        return 0;
    size_t row = y + global_cfg.rowoff;
    if (row > global_cfg.num_bytes / columns)
        return 0;
    *start = row * columns;
    size_t remaining = global_cfg.num_bytes - *start;
    return remaining < columns ? remaining : columns;
}

void draw_row_text(size_t y, append_buffer *ab) {
    size_t start = 0;
    size_t len = displayed_row_length(y, &start);
    if (len)
        append_clipped(ab, (const char *)&global_cfg.file[start], len,
                       global_cfg.screencols, 0);
}

void draw_row_hex(size_t y, append_buffer *ab) {
    size_t start = 0;
    size_t len = displayed_row_length(y, &start);
    if (!len) {
        if (global_cfg.screencols)
            append_to_buffer(ab, "~", 1);
        return;
    }

    size_t offset_width = editor_offset_width();
    if (global_cfg.screencols < offset_width + 9)
        return;
    size_t columns = (global_cfg.screencols - offset_width - 5) / 4;
    if (columns > global_cfg.cur_screencols)
        columns = global_cfg.cur_screencols;
    if (len > columns)
        len = columns;
    append_offset(ab, start, 0);

    for (size_t j = 0; j < columns; ++j) {
        if (j < len) {
            append_hex_byte(ab, start + j);
            append_to_buffer(ab, " ", 1);
        } else {
            append_spaces(ab, 3);
        }
    }
    append_to_buffer(ab, " |", 2);
    for (size_t j = 0; j < columns; ++j) {
        char byte = j < len ? printable_byte(global_cfg.file[start + j]) : ' ';
        int selected = j < len && start + j == global_cfg.cur_byte;
        append_selected(ab, &byte, 1, selected);
    }
    append_to_buffer(ab, "|", 1);
}

void draw_row_disassembler(size_t y, append_buffer *ab) {
    if (!global_cfg.disassembler_buffer || y >= global_cfg.screenrows)
        return;
    const disassemblerRow *row = &global_cfg.disassembler_buffer[y];
    if (row->start_byte >= row->end_byte || row->start_byte >= global_cfg.num_bytes) {
        if (global_cfg.screencols)
            append_to_buffer(ab, "~", 1);
        return;
    }

    size_t offset_width = editor_offset_width();
    if (global_cfg.screencols <= offset_width + 2)
        return;
    int selected = row->start_byte <= global_cfg.cur_byte &&
                   global_cfg.cur_byte < row->end_byte;
    append_offset(ab, row->start_byte, selected);
    size_t available = global_cfg.screencols - offset_width - 2;

    /* Keep at least 24 cells for the instruction before adding raw bytes. */
    if (available >= 30 + 2 + 24) {
        for (size_t j = 0; j < 15; ++j) {
            if (global_cfg.file && j < row->end_byte - row->start_byte &&
                j < global_cfg.num_bytes - row->start_byte) {
                append_hex_byte(ab, row->start_byte + j);
            } else {
                append_spaces(ab, 2);
            }
        }
        append_to_buffer(ab, "  ", 2);
        available -= 32;
    }
    if (selected)
        append_to_buffer(ab, "\x1b[7m", 4);
    append_clipped(ab, row->diss_str, strnlen(row->diss_str, sizeof(row->diss_str)),
                   available, 1);
    if (selected)
        append_to_buffer(ab, "\x1b[m", 3);
}

void editor_draw_rows(append_buffer *ab) {
    if (global_cfg.mode == DISASSEMBLER_MODE && global_cfg.disassembler_buffer)
        disassemble_block(global_cfg.cur_byte);

    for (size_t y = 0; y < global_cfg.screenrows; ++y) {
        append_to_buffer(ab, "\x1b[K", 3);
        if (global_cfg.mode == TEXT_MODE) {
            if (y + global_cfg.rowoff >= global_cfg.numrows) {
                if (!global_cfg.num_bytes && y == global_cfg.screenrows / 3) {
                    const char welcome[] = "LHiew binary viewer " EDITOR_VERSION;
                    size_t len = sizeof(welcome) - 1;
                    if (len > global_cfg.screencols)
                        len = global_cfg.screencols;
                    append_spaces(ab, (global_cfg.screencols - len) / 2);
                    append_clipped(ab, welcome, sizeof(welcome) - 1, len, 1);
                } else if (global_cfg.screencols) {
                    append_to_buffer(ab, "~", 1);
                }
            } else {
                draw_row_text(y, ab);
            }
        } else if (global_cfg.mode == HEX_MODE) {
            draw_row_hex(y, ab);
        } else {
            draw_row_disassembler(y, ab);
        }
        append_to_buffer(ab, "\r\n", 2);
    }
}

static void draw_architecture_menu(append_buffer *ab) {
    size_t count = architecture_profile_count();
    size_t visible = global_cfg.screenrows > 1 ? global_cfg.screenrows - 1 : 1;
    size_t choice = global_cfg.architecture_choice;
    if (count && choice >= count)
        choice = count - 1;
    size_t first = choice / visible * visible;
    size_t current = architecture_current_profile();
    for (size_t y = 0; y < global_cfg.screenrows; ++y) {
        append_to_buffer(ab, "\x1b[K", 3);
        if (!y) {
            char title[160];
            snprintf(title, sizeof(title), "Architecture %zu/%zu%s%s", choice + 1, count,
                     global_cfg.screencols >= 64 ? " | " : "",
                     global_cfg.screencols >= 64 ? architecture_detection_label() : "");
            append_clipped(ab, title, strlen(title), global_cfg.screencols, 1);
        } else if (first + y - 1 < count) {
            size_t index = first + y - 1;
            const architectureProfile *profile = architecture_profile(index);
            char prefix[] = "   ";
            prefix[0] = index == choice ? '>' : ' ';
            prefix[1] = index == current ? '*' : ' ';
            if (index == choice)
                append_to_buffer(ab, "\x1b[7m", 4);
            size_t prefix_width = global_cfg.screencols < 3 ? global_cfg.screencols : 3;
            append_clipped(ab, prefix, 3, prefix_width, 0);
            append_clipped(ab, profile->name, strlen(profile->name),
                           global_cfg.screencols - prefix_width, 1);
            if (index == choice)
                append_to_buffer(ab, "\x1b[m", 3);
        }
        append_to_buffer(ab, "\r\n", 2);
    }
}

void editor_draw_prompt(append_buffer *ab, const char *title, const char *input,
                        size_t length, const char *help) {
    size_t width = global_cfg.screencols;
    size_t room = width > 3 ? width - 3 : 0;
    size_t start = length > room ? length - room : 0;
    for (size_t y = 0; y < global_cfg.screenrows; ++y) {
        append_to_buffer(ab, "\x1b[K", 3);
        if (y == 1) {
            append_clipped(ab, start ? "< " : "> ", 2, width, 0);
            append_clipped(ab, input + start, length - start, room, 0);
        } else {
            const char *line = !y ? title : y == 2 ? help : "";
            append_clipped(ab, line, strlen(line), width, 1);
        }
        append_to_buffer(ab, "\r\n", 2);
    }
}

static void draw_edit_prompt(append_buffer *ab) {
    if (global_cfg.goto_prompt && !global_cfg.edit_exit_prompt) {
        char help[64];
        snprintf(help, sizeof(help), "Range: 0..%zx", global_cfg.num_bytes);
        editor_draw_prompt(ab, "Go to file offset (hex)", global_cfg.goto_input,
                           global_cfg.goto_length, help);
        return;
    }
    const char *save = global_cfg.edit_exit_prompt == 2 ? "s Save and quit" : "s Save and leave edit mode";
    for (size_t y = 0; y < global_cfg.screenrows; ++y) {
        const char *line = !y ? "Unsaved changes" : y == 1 ? save
            : y == 2 ? "d Discard unsaved changes" : "";
        append_to_buffer(ab, "\x1b[K", 3);
        append_clipped(ab, line, strlen(line), global_cfg.screencols, 1);
        append_to_buffer(ab, "\r\n", 2);
    }
}

void editor_draw_status_bar(append_buffer *ab) {
    size_t width = global_cfg.screencols;
    char mode[48], status[160];
    int compact = width < 64;
    if (global_cfg.editing) {
        snprintf(mode, sizeof(mode), "EDIT %s%s", global_cfg.edit_ascii ? "ASCII" : "HEX",
                 hex_edit_dirty_count() ? "*" : "");
    } else if (global_cfg.mode == DISASSEMBLER_MODE && global_cfg.architecture != ARCH_X86) {
        snprintf(mode, sizeof(mode), "ASM %s", architecture_current_name());
    } else if (global_cfg.mode == DISASSEMBLER_MODE) {
        const char *bits = "32";
        if (global_cfg.disassembler_mode == REAL)
            bits = "16R";
        else if (global_cfg.disassembler_mode == MODE_LONG_COMPAT_16)
            bits = "16";
        else if (global_cfg.disassembler_mode == MODE_LONG_COMPAT_64)
            bits = "64";
        snprintf(mode, sizeof(mode), compact ? "ASM%s" : "Disassembler %s-bit", bits);
    } else {
        snprintf(mode, sizeof(mode), "%s", global_cfg.mode == HEX_MODE
                 ? (compact ? "HEX" : HEX_MODE_STR)
                 : (compact ? "TEXT" : TEXT_MODE_STR));
    }
    snprintf(status, sizeof(status), "%s %zu:%zu", mode,
             global_cfg.cur_byte, global_cfg.num_bytes);
    const binaryInfo *info = architecture_binary_info();
    const char *selection = global_cfg.architecture_manual ? "manual"
        : !info || info->status == BINARY_RAW ? "auto/raw" : "auto";
    if (global_cfg.mode == DISASSEMBLER_MODE && strlen(status) + strlen(selection) + 3 <= width)
        snprintf(status, sizeof(status), "%s [%s] %zu:%zu", mode, selection,
                 global_cfg.cur_byte, global_cfg.num_bytes);
    if (strlen(status) > width)
        snprintf(status, sizeof(status), "%s @%zx", mode, global_cfg.cur_byte);
    size_t status_len = strlen(status);
    if (status_len > width)
        status_len = width;

    const char *filename = global_cfg.filename ? global_cfg.filename : "[No file]";
    const char *basename = strrchr(filename, '/');
    if (basename)
        filename = basename + 1;
    size_t filename_width = width > status_len ? width - status_len - 1 : 0;
    size_t filename_len = strlen(filename);
    if (filename_len > filename_width)
        filename_len = filename_width;
    append_to_buffer(ab, "\x1b[7m", 4);
    append_clipped(ab, filename, strlen(filename), filename_len, 1);
    append_spaces(ab, width - filename_len - status_len);
    append_clipped(ab, status, strlen(status), status_len, 1);
    append_to_buffer(ab, "\x1b[m\r\n", 5);
}

void editor_draw_message_bar(append_buffer *ab) {
    append_to_buffer(ab, "\x1b[K", 3);
    const char *message = global_cfg.statusmsg;
    int expired = !message[0] || time(NULL) - global_cfg.statusmsg_time >= 5;
    if (global_cfg.edit_exit_prompt) {
        message = global_cfg.screencols < 40 ? "s save d discard Esc stay"
            : "s save | d discard | Esc continue editing";
    } else if (global_cfg.goto_prompt && expired) {
        message = "Enter go | Esc cancel";
    } else if (global_cfg.search_prompt && expired) {
        message = search_prompt_message();
    } else if (global_cfg.executable_browser && expired) {
        message = executable_browser_message();
    } else if (global_cfg.architecture_menu) {
        message = global_cfg.screencols < 64 ? "Enter apply | Esc cancel"
            : "Up/Down j/k | PgUp/PgDn | Enter apply | Esc cancel | Ctrl-Q quit";
    } else if (expired) {
        if (global_cfg.editing) {
            message = global_cfg.screencols < 40 ? "F9 save Tab Esc exit"
                : "F9 save | Tab HEX/ASCII | F5 goto | F7 find | Esc/F10 exit | ^Q quit";
        } else if (global_cfg.screencols < 40) {
            message = "^Q quit m F3 edit g goto s find";
        } else if (global_cfg.screencols < 72) {
            message = "^Q quit | m mode | F3 edit | g goto | s find | a arch";
        } else {
            message = "^Q quit | m mode | F3 edit | F5/g goto | F7/s find n/N next"
                " | F8/b imports | a arch | e entry";
        }
    }
    append_clipped(ab, message, strlen(message), global_cfg.screencols, 1);
}

void editor_scroll(void) {
    if (!global_cfg.screenrows || !global_cfg.cur_screencols)
        return;
    if (global_cfg.cy < global_cfg.rowoff)
        global_cfg.rowoff = global_cfg.cy;
    if (global_cfg.cy - global_cfg.rowoff >= global_cfg.screenrows)
        global_cfg.rowoff = global_cfg.cy - global_cfg.screenrows + 1;
}

static void draw_resize_message(append_buffer *ab) {
    append_to_buffer(ab, "\x1b[2J", 4);
    size_t rows = global_cfg.terminal_rows;
    size_t cols = global_cfg.screencols;
    if (!rows || !cols)
        return;
    char minimum[64];
    const char *lines[3];
    size_t line_count;
    if (rows == 1) {
        snprintf(minimum, sizeof(minimum), "Resize %dx%d | ^Q quit", SCREENCOLS_MIN, SCREENROWS_MIN);
        lines[0] = minimum;
        line_count = 1;
    } else if (rows == 2) {
        snprintf(minimum, sizeof(minimum), "Resize to %dx%d", SCREENCOLS_MIN, SCREENROWS_MIN);
        lines[0] = minimum;
        lines[1] = "Ctrl-Q quit";
        line_count = 2;
    } else {
        snprintf(minimum, sizeof(minimum), cols < SCREENCOLS_MIN
                 ? "Need %dx%d" : "Resize to at least %dx%d", SCREENCOLS_MIN, SCREENROWS_MIN);
        lines[0] = cols < SCREENCOLS_MIN ? "Too small" : "Window too small";
        lines[1] = minimum;
        lines[2] = cols < SCREENCOLS_MIN ? "Ctrl-Q quit" : "Ctrl-Q to quit";
        line_count = 3;
    }
    size_t first_row = (rows - line_count) / 2 + 1;
    for (size_t i = 0; i < line_count; ++i) {
        size_t len = strlen(lines[i]);
        if (len > cols)
            len = cols;
        append_position(ab, first_row + i, (cols - len) / 2 + 1);
        append_clipped(ab, lines[i], strlen(lines[i]), len, 0);
    }
}

void editor_draw_screen(append_buffer *ab) {
    append_to_buffer(ab, "\x1b[?25l\x1b[H", 9);
    if (global_cfg.window_too_small) {
        draw_resize_message(ab);
        if (global_cfg.edit_exit_prompt && global_cfg.terminal_rows && global_cfg.screencols) {
            append_position(ab, 1, 1);
            append_to_buffer(ab, "\x1b[K", 3);
            const char *message = "s save d discard Esc stay";
            append_clipped(ab, message, strlen(message), global_cfg.screencols, 1);
        }
        append_position(ab, 1, 1);
        return;
    }
    int edit_view_valid = 1;
    if (global_cfg.goto_prompt || global_cfg.edit_exit_prompt) {
        draw_edit_prompt(ab);
    } else if (global_cfg.search_prompt) {
        search_draw_prompt(ab);
    } else if (global_cfg.executable_browser) {
        executable_browser_draw(ab);
    } else if (global_cfg.architecture_menu) {
        draw_architecture_menu(ab);
    } else if (global_cfg.editing && !(edit_view_valid = hex_edit_check_file())) {
        /* A truncated file can invalidate mapped pages; keep the error and
           exit controls visible without reading the old mapping. */
        for (size_t y = 0; y < global_cfg.screenrows; ++y) {
            append_to_buffer(ab, "\x1b[K", 3);
            if (!y) {
                const char *message = "File changed; Esc to leave editing";
                append_clipped(ab, message, strlen(message), global_cfg.screencols, 1);
            }
            append_to_buffer(ab, "\r\n", 2);
        }
    } else {
        editor_scroll();
        editor_draw_rows(ab);
    }
    editor_draw_status_bar(ab);
    editor_draw_message_bar(ab);
    if (!global_cfg.edit_exit_prompt && (global_cfg.goto_prompt || global_cfg.search_prompt ||
        (global_cfg.executable_browser && global_cfg.executable_prompt))) {
        size_t length = global_cfg.goto_prompt ? global_cfg.goto_length
            : global_cfg.search_prompt ? global_cfg.search_input_length : global_cfg.executable_input_length;
        size_t column = length + 3;
        if (column > global_cfg.screencols) column = global_cfg.screencols;
        append_position(ab, 2, column);
        append_to_buffer(ab, "\x1b[?25h", 6);
    } else if (global_cfg.executable_browser && !global_cfg.edit_exit_prompt) {
        append_position(ab, 1, 1);
    } else if (global_cfg.editing && edit_view_valid && !global_cfg.edit_exit_prompt &&
               global_cfg.cur_byte < global_cfg.num_bytes) {
        size_t col = global_cfg.edit_ascii
            ? editor_offset_width() + 5 + 3 * global_cfg.cur_screencols + global_cfg.cx
            : editor_offset_width() + 3 + 3 * global_cfg.cx + (size_t)global_cfg.edit_nibble;
        append_position(ab, global_cfg.cy - global_cfg.rowoff + 1, col);
        append_to_buffer(ab, "\x1b[?25h", 6);
    } else if (!global_cfg.architecture_menu && !global_cfg.edit_exit_prompt &&
        global_cfg.mode == TEXT_MODE &&
        global_cfg.screenrows && global_cfg.screencols) {
        size_t cursor_row = global_cfg.cy - global_cfg.rowoff;
        size_t cursor_col = global_cfg.cx;
        if (cursor_row >= global_cfg.screenrows)
            cursor_row = global_cfg.screenrows - 1;
        if (cursor_col >= global_cfg.screencols)
            cursor_col = global_cfg.screencols - 1;
        append_position(ab, cursor_row + 1, cursor_col + 1);
        append_to_buffer(ab, "\x1b[?25h", 6);
    } else {
        append_position(ab, 1, 1);
    }
}

void editor_refresh_screen(void) {
    editor_update_window_size();
    append_buffer ab = ABUF_INIT;
    editor_draw_screen(&ab);
    int written = terminal_write(ab.buffer, ab.len);
    int error = errno;
    free_append_buffer(&ab);
    if (!written) {
        errno = error;
        die_safely("write terminal");
    }
}

void editor_set_status_message(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(global_cfg.statusmsg, sizeof(global_cfg.statusmsg), fmt, ap);
    va_end(ap);
    global_cfg.statusmsg_time = time(NULL);
}
