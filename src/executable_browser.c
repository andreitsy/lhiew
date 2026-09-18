#include "lhiew/types.h"
#include "lhiew/executable_browser.h"
#include "lhiew/executable.h"
#include "lhiew/editor.h"
#include "lhiew/hex_edit.h"
#include "lhiew/input.h"
#include "lhiew/render.h"
#include "lhiew/terminal.h"

#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static executableInfo browser;

static int file_ready(void) {
    if (!hex_edit_check_file()) return 0;
    if (global_cfg.fp) {
        struct stat st;
        if (fstat(fileno(global_cfg.fp), &st) || st.st_size < 0 ||
            (uintmax_t)st.st_size != global_cfg.num_bytes) {
            editor_set_status_message("File size changed; reopen before browsing");
            return 0;
        }
    }
    return 1;
}

static int visible(const executableRow *row) {
    return global_cfg.executable_imports
        ? row->kind == EXE_MODULE || row->kind == EXE_IMPORT
        : row->kind == EXE_HEADER || row->kind == EXE_REGION;
}

static size_t visible_count(void) {
    size_t count = 0;
    for (size_t i = 0; i < browser.count; ++i)
        if (visible(&browser.rows[i])) count++;
    return count;
}

static executableRow *selected(void) {
    size_t count = 0;
    for (size_t i = 0; i < browser.count; ++i) {
        if (visible(&browser.rows[i]) && count++ == global_cfg.executable_choice)
            return &browser.rows[i];
    }
    return NULL;
}

static void refresh(void) {
    executable_parse(global_cfg.file, global_cfg.num_bytes, &browser);
    size_t count = visible_count();
    if (global_cfg.executable_choice >= count)
        global_cfg.executable_choice = count ? count - 1 : 0;
}

void executable_browser_open(void) {
    if (!file_ready()) return;
    global_cfg.executable_imports = 1;
    global_cfg.executable_choice = 0;
    global_cfg.executable_prompt = 0;
    refresh();
    global_cfg.executable_browser = 1;
    global_cfg.statusmsg[0] = '\0';
}

void executable_browser_close(void) {
    global_cfg.executable_browser = 0;
    global_cfg.executable_prompt = 0;
    executable_free(&browser);
}

static void start_edit(void) {
    if (!file_ready()) return;
    refresh();
    const executableRow *row = selected();
    if (browser.status != EXE_OK || !row || !row->editable) {
        editor_set_status_message("This record is not available for import editing");
        return;
    }
    if (!hex_edit_begin()) return;
    global_cfg.mode = HEX_MODE;
    switch_mode();
    if (row->name_offset != SIZE_MAX) {
        memcpy(global_cfg.executable_input, global_cfg.file + row->name_offset, row->name_length);
        global_cfg.executable_input[row->name_length] = '\0';
        global_cfg.executable_input_length = row->name_length;
        global_cfg.executable_prompt = 1;
    } else if (row->ordinal_offset != SIZE_MAX) {
        snprintf(global_cfg.executable_input, sizeof(global_cfg.executable_input), "%u", row->ordinal);
        global_cfg.executable_input_length = strlen(global_cfg.executable_input);
        global_cfg.executable_prompt = 2;
    }
    global_cfg.statusmsg[0] = '\0';
}

static void apply_edit(void) {
    if (!file_ready()) return;
    const executableRow *row = selected();
    if (!row || browser.status != EXE_OK || !row->editable) return;
    hexPatch patches[2];
    size_t count = 1;
    uint8_t encoded[8];
    if (global_cfg.executable_prompt == 1) {
        if (global_cfg.executable_input_length != row->name_length) {
            editor_set_status_message("Name must contain exactly %zu bytes", row->name_length);
            return;
        }
        patches[0] = (hexPatch){row->name_offset,
            (const uint8_t *)global_cfg.executable_input, row->name_length};
    } else {
        uint32_t ordinal = 0;
        int valid = global_cfg.executable_input_length != 0;
        for (size_t i = 0; valid && i < global_cfg.executable_input_length; ++i) {
            unsigned digit = (unsigned char)global_cfg.executable_input[i] - '0';
            if (digit > 9 || digit > row->ordinal_max ||
                ordinal > (row->ordinal_max - digit) / 10)
                valid = 0;
            else
                ordinal = ordinal * 10 + digit;
        }
        if (!valid) {
            editor_set_status_message("Ordinal must be decimal 0..%u", row->ordinal_max);
            return;
        }
        uint64_t value = row->ordinal_flags | ordinal;
        for (unsigned i = 0; i < row->ordinal_width; ++i)
            encoded[i] = (uint8_t)(value >> (8 * i));
        patches[0] = (hexPatch){row->ordinal_offset, encoded, row->ordinal_width};
        if (row->mirror_offset != SIZE_MAX && row->mirror_offset != row->ordinal_offset)
            patches[count++] = (hexPatch){row->mirror_offset, encoded, row->ordinal_width};
    }
    if (!hex_edit_patch(patches, count)) return;
    global_cfg.executable_prompt = 0;
    refresh();
    editor_set_status_message("Import edit staged; F9 saves, Esc returns to hex");
}

static void prompt_key(int key) {
    if (key == '\x1b') {
        global_cfg.executable_prompt = 0;
        global_cfg.statusmsg[0] = '\0';
    } else if (key == '\r' || key == '\n') {
        apply_edit();
    } else if (editor_prompt_key(global_cfg.executable_input, &global_cfg.executable_input_length,
                                 sizeof(global_cfg.executable_input), key) > 0) {
        global_cfg.statusmsg[0] = '\0';
    }
}

void executable_browser_keypress(int key) {
    if (global_cfg.executable_prompt) {
        prompt_key(key);
        return;
    }
    switch (key) {
        case '\x1b':
        case F8_KEY:
            if (!file_ready()) return;
            executable_browser_close();
            return;
        case '\t':
            global_cfg.executable_imports = !global_cfg.executable_imports;
            global_cfg.executable_choice = 0;
            global_cfg.statusmsg[0] = '\0';
            return;
        case F3_KEY:
            start_edit();
            return;
        case F9_KEY:
            if (hex_edit_save() && file_ready()) refresh();
            return;
        case '\r':
        case '\n': {
            if (!file_ready()) return;
            const executableRow *row = selected();
            if (row && row->offset < global_cfg.num_bytes) {
                global_cfg.cur_byte = row->offset;
                global_cfg.mode = HEX_MODE;
                switch_mode();
                executable_browser_close();
            }
            return;
        }
    }
    global_cfg.executable_choice = editor_menu_choice(global_cfg.executable_choice,
                                                     visible_count(), key);
}

static void append_line(append_buffer *ab, const char *text, size_t width) {
    size_t length = strlen(text);
    for (size_t i = 0; i < length && i < width; ++i) {
        unsigned char byte = (unsigned char)text[i];
        char cell = byte >= 32 && byte < 127 ? (char)byte : '?';
        if (i + 1 == width && length > width) cell = '~';
        append_to_buffer(ab, &cell, 1);
    }
}

void executable_browser_draw(append_buffer *ab) {
    if (global_cfg.executable_prompt) {
        const executableRow *edit = selected();
        char title[80];
        if (global_cfg.executable_prompt == 1)
            snprintf(title, sizeof(title), "Edit name: exactly %zu bytes", edit ? edit->name_length : 0);
        else
            snprintf(title, sizeof(title), "Edit ordinal (decimal)");
        editor_draw_prompt(ab, title, global_cfg.executable_input, global_cfg.executable_input_length,
                           "Ctrl-U clears | Enter stages");
        return;
    }
    size_t count = visible_count();
    size_t visible_rows = global_cfg.screenrows > 1 ? global_cfg.screenrows - 1 : 1;
    size_t first = global_cfg.executable_choice / visible_rows * visible_rows;
    size_t position = 0, index = 0;
    for (size_t y = 0; y < global_cfg.screenrows; ++y) {
        append_to_buffer(ab, "\x1b[K", 3);
        char line[640] = "";
        int highlight = 0;
        if (!y) {
            snprintf(line, sizeof(line), "%s %s %zu/%zu%s", browser.format,
                     global_cfg.executable_imports ? "Imports" : "Headers/regions",
                     count ? global_cfg.executable_choice + 1 : 0, count,
                     browser.status == EXE_OK ? "" : " [incomplete]");
        } else if (!count && y == 1) {
            snprintf(line, sizeof(line), "%s", browser.status == EXE_OK
                     ? (global_cfg.executable_imports ? "No imports" : "No header records")
                     : browser.message);
        } else {
            while (index < browser.count) {
                const executableRow *row = &browser.rows[index++];
                if (!visible(row)) continue;
                if (position++ < first) continue;
                highlight = position - 1 == global_cfg.executable_choice;
                snprintf(line, sizeof(line), "%c %0*zx %s", highlight ? '>' : ' ',
                         (int)editor_offset_width(), row->offset, row->label);
                break;
            }
        }
        if (highlight) append_to_buffer(ab, "\x1b[7m", 4);
        append_line(ab, line, global_cfg.screencols);
        if (highlight) append_to_buffer(ab, "\x1b[m", 3);
        append_to_buffer(ab, "\r\n", 2);
    }
}

const char *executable_browser_message(void) {
    if (global_cfg.executable_prompt) return "Enter stage | ^U clear | Esc cancel";
    if (browser.message[0]) return browser.message;
    return global_cfg.screencols < 40 ? "Tab view F3 edit Esc back"
        : "Tab imports/headers | Enter hex | F3 edit | F9 save | Esc back";
}
