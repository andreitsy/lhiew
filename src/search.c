#include "lhiew/types.h"
#include "lhiew/search.h"
#include "lhiew/editor.h"
#include "lhiew/hex_edit.h"
#include "lhiew/input.h"
#include "lhiew/render.h"

#include <string.h>

int search_compile(const char *text, size_t text_length, int ascii,
                   uint8_t *pattern, size_t *length) {
    if (!length)
        return 0;
    *length = 0;
    if (!text || !pattern)
        return 0;
    if (ascii) {
        if (text_length > SEARCH_PATTERN_MAX)
            return 0;
        memcpy(pattern, text, text_length);
        *length = text_length;
        return 1;
    }
    int high = -1;
    size_t count = 0;
    for (size_t i = 0; i < text_length; ++i) {
        unsigned char c = (unsigned char)text[i];
        if (c == ' ' || c == '\t') {
            /* A separator may only fall between complete bytes. */
            if (high >= 0)
                return 0;
            continue;
        }
        int digit = editor_hex_digit(c);
        if (digit < 0)
            return 0;
        if (high < 0) {
            high = digit;
            continue;
        }
        if (count == SEARCH_PATTERN_MAX)
            return 0;
        pattern[count++] = (uint8_t)(high << 4 | digit);
        high = -1;
    }
    /* A trailing single digit is an incomplete byte, not a one-nibble pattern. */
    if (high >= 0)
        return 0;
    *length = count;
    return 1;
}

int search_find(const uint8_t *data, size_t size, const uint8_t *pattern,
                size_t length, size_t from, int backward, size_t *found) {
    if (!data || !pattern || !found || !length || length > size)
        return 0;
    size_t last = size - length;
    if (backward) {
        if (from > last)
            from = last;
        for (size_t start = from + 1; start--;) {
            if (data[start] == pattern[0] &&
                !memcmp(data + start, pattern, length)) {
                *found = start;
                return 1;
            }
        }
        return 0;
    }
    for (size_t start = from; start <= last;) {
        const uint8_t *hit = memchr(data + start, pattern[0], last - start + 1);
        if (!hit)
            return 0;
        start = (size_t)(hit - data);
        if (!memcmp(data + start, pattern, length)) {
            *found = start;
            return 1;
        }
        start++;
    }
    return 0;
}

void search_open_prompt(void) {
    global_cfg.search_prompt = 1;
    global_cfg.edit_nibble = 0;
    global_cfg.statusmsg[0] = '\0';
    /* Keep the previous text so a near-miss pattern can be corrected. */
    global_cfg.search_input[global_cfg.search_input_length] = '\0';
}

/* Run the compiled pattern and report the outcome. */
static int run_search(size_t from, int backward) {
    if (!global_cfg.search_pattern_length) {
        editor_set_status_message("Enter bytes to search for");
        return 0;
    }
    if (global_cfg.editing && !hex_edit_check_file())
        return 0;
    size_t found = 0;
    if (!search_find(global_cfg.file, global_cfg.num_bytes,
                     global_cfg.search_pattern, global_cfg.search_pattern_length,
                     from, backward, &found)) {
        editor_set_status_message("Pattern not found %s offset %zx",
                                  backward ? "before" : "after", global_cfg.cur_byte);
        return 0;
    }
    global_cfg.cur_byte = found;
    global_cfg.search_backward = backward;
    switch_mode();
    editor_set_status_message("Found %zu byte%s at %zx", global_cfg.search_pattern_length,
                              global_cfg.search_pattern_length == 1 ? "" : "s", found);
    return 1;
}

int search_repeat(int backward) {
    global_cfg.search_backward = backward != 0;
    if (!global_cfg.search_pattern_length) {
        editor_set_status_message("No previous search; F7 enters a pattern");
        return 0;
    }
    size_t from = global_cfg.cur_byte;
    if (backward) {
        if (!from) {
            editor_set_status_message("Pattern not found before offset 0");
            return 0;
        }
        from--;
    } else {
        /* Advance so a repeat leaves the match the cursor already sits on. */
        if (from >= global_cfg.num_bytes) {
            editor_set_status_message("Pattern not found after offset %zx", from);
            return 0;
        }
        from++;
    }
    return run_search(from, backward);
}

void search_keypress(int key) {
    if (key == '\x1b') {
        global_cfg.search_prompt = 0;
        global_cfg.statusmsg[0] = '\0';
        return;
    }
    if (key == '\t') {
        global_cfg.search_ascii = !global_cfg.search_ascii;
        global_cfg.statusmsg[0] = '\0';
        return;
    }
    if (key == '\r' || key == '\n') {
        uint8_t pattern[SEARCH_PATTERN_MAX];
        size_t length = 0;
        if (!search_compile(global_cfg.search_input, global_cfg.search_input_length,
                            global_cfg.search_ascii, pattern, &length)) {
            editor_set_status_message(global_cfg.search_ascii
                                      ? "Text is longer than %d bytes"
                                      : "Enter complete hex byte pairs (max %d)",
                                      SEARCH_PATTERN_MAX);
            return;
        }
        if (!length) {
            editor_set_status_message("Enter bytes to search for");
            return;
        }
        memcpy(global_cfg.search_pattern, pattern, length);
        global_cfg.search_pattern_length = length;
        global_cfg.search_prompt = 0;
        /* Search from the cursor so a pattern under it is reported in place. */
        run_search(global_cfg.cur_byte, global_cfg.search_backward);
        return;
    }
    size_t room = global_cfg.search_ascii ? SEARCH_PATTERN_MAX
        : sizeof(global_cfg.search_input) - 1;
    int edited = editor_prompt_key(global_cfg.search_input, &global_cfg.search_input_length,
                                   room + 1, key);
    if (edited < 0)
        editor_set_status_message("Pattern is limited to %d bytes", SEARCH_PATTERN_MAX);
    else if (edited)
        global_cfg.statusmsg[0] = '\0';
}

void search_draw_prompt(append_buffer *ab) {
    char title[64];
    snprintf(title, sizeof(title), "Search %s (%s)",
             global_cfg.search_ascii ? "text" : "hex bytes",
             global_cfg.search_backward ? "backward" : "forward");
    editor_draw_prompt(ab, title, global_cfg.search_input, global_cfg.search_input_length,
                       "Tab hex/text | Ctrl-U clears");
}

const char *search_prompt_message(void) {
    return global_cfg.screencols < 40 ? "Enter find | Esc cancel"
        : "Enter find | Tab hex/text | Esc cancel";
}
