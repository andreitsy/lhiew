#include "lhiew/types.h"
#include "lhiew/input.h"
#include "lhiew/architecture.h"
#include "lhiew/disassembler.h"
#include "lhiew/editor.h"
#include "lhiew/executable_browser.h"
#include "lhiew/hex_edit.h"
#include "lhiew/render.h"
#include "lhiew/search.h"
#include "lhiew/terminal.h"

#include <stdlib.h>
#include <unistd.h>

static size_t disassembler_cursor_row(size_t position) {
    if (global_cfg.disassembler_buffer) {
        for (size_t row = 0; row < global_cfg.screenrows; ++row) {
            const disassemblerRow *instruction = &global_cfg.disassembler_buffer[row];
            if (instruction->start_byte <= position && position < instruction->end_byte)
                return row;
        }
    }
    return global_cfg.screenrows;
}

static size_t disassembler_page_position(size_t position, int key) {
    size_t remaining = global_cfg.screenrows;
    size_t shift = 0;
    int preserve_shift = position < global_cfg.num_bytes;
    if (!remaining || !global_cfg.disassembler_buffer)
        return position;

    for (;;) {
        disassemble_block(position);
        size_t row = disassembler_cursor_row(position);
        if (row == global_cfg.screenrows) {
            if (key == PAGE_UP && remaining && position == global_cfg.num_bytes && position) {
                position--;
                remaining--;
                continue;
            }
            return position;
        }
        const disassemblerRow *current = &global_cfg.disassembler_buffer[row];
        if (preserve_shift) {
            shift = position - current->start_byte;
            preserve_shift = 0;
        }
        if (!remaining) {
            size_t length = current->end_byte - current->start_byte;
            return current->start_byte + (shift < length ? shift : length - 1);
        }

        /* Consume the rows already decoded before refreshing another viewport. */
        if (key == PAGE_UP) {
            size_t steps = row < remaining ? row : remaining;
            if (steps) {
                position = global_cfg.disassembler_buffer[row - steps].start_byte;
                remaining -= steps;
            } else if (current->start_byte) {
                position = current->start_byte - 1;
                remaining--;
            } else {
                return 0;
            }
        } else {
            size_t steps = 0;
            while (steps < remaining && row + steps + 1 < global_cfg.screenrows) {
                const disassemblerRow *next = &global_cfg.disassembler_buffer[row + steps + 1];
                if (next->start_byte >= next->end_byte)
                    break;
                steps++;
            }
            if (steps) {
                position = global_cfg.disassembler_buffer[row + steps].start_byte;
                remaining -= steps;
            } else {
                position = current->end_byte;
                remaining--;
            }
        }
    }
}

void editor_move_cursor(int key) {
    if (global_cfg.window_too_small || !global_cfg.num_bytes)
        return;

    size_t position = global_cfg.cur_byte;
    size_t width = global_cfg.cur_screencols;
    global_cfg.edit_nibble = 0;
    if (key == CTRL_HOME || key == CTRL_END) {
        global_cfg.cur_byte = key == CTRL_HOME ? 0 : global_cfg.num_bytes - 1;
        switch_mode();
        return;
    }
    if (global_cfg.mode == DISASSEMBLER_MODE) {
        if (key == PAGE_UP || key == PAGE_DOWN) {
            global_cfg.cur_byte = disassembler_page_position(position, key);
            switch_mode();
            return;
        }
        disassemble_block(position);
        size_t row = disassembler_cursor_row(position);
        size_t start = row < global_cfg.screenrows
            ? global_cfg.disassembler_buffer[row].start_byte : position;
        size_t shift = position - start;
        switch (key) {
            case HOME_KEY:
                position = start;
                break;
            case END_KEY:
                if (row < global_cfg.screenrows)
                    position = global_cfg.disassembler_buffer[row].end_byte - 1;
                break;
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
            case HOME_KEY:
                position -= position % width;
                break;
            case END_KEY: {
                size_t remaining = global_cfg.num_bytes - position;
                size_t distance = width - position % width - 1;
                if (remaining)
                    position += distance < remaining ? distance : remaining - 1;
                break;
            }
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
            case PAGE_UP:
            case PAGE_DOWN: {
                size_t distance = global_cfg.screenrows > SIZE_MAX / width
                    ? SIZE_MAX : global_cfg.screenrows * width;
                if (key == PAGE_UP) {
                    position -= position < distance ? position : distance;
                } else {
                    size_t remaining = global_cfg.num_bytes - position;
                    position += remaining < distance ? remaining : distance;
                }
                break;
            }
        }
    }
    global_cfg.cur_byte = position;
    switch_mode();
}

static void change_mode(editorMode mode) {
    global_cfg.mode = mode;
    if (mode == DISASSEMBLER_MODE && !global_cfg.cur_byte)
        architecture_jump_to_entry();
    switch_mode();
}

static void architecture_menu_keypress(int key) {
    size_t count = architecture_profile_count();
    if (!count)
        return;
    size_t choice = global_cfg.architecture_choice;
    if (choice >= count)
        choice = count - 1;
    size_t page = global_cfg.screenrows > 1 ? global_cfg.screenrows - 1 : 1;
    switch (key) {
        case '\x1b':
            global_cfg.architecture_menu = 0;
            return;
        case '\r':
        case '\n':
            architecture_select(choice);
            global_cfg.architecture_menu = 0;
            return;
        case ARROW_UP:
        case 'k':
            if (choice)
                choice--;
            break;
        case ARROW_DOWN:
        case 'j':
            if (choice + 1 < count)
                choice++;
            break;
        case PAGE_UP:
            choice -= choice < page ? choice : page;
            break;
        case PAGE_DOWN:
            choice += count - choice - 1 < page ? count - choice - 1 : page;
            break;
    }
    global_cfg.architecture_choice = choice;
}

static void quit_editor(void) {
    write(STDOUT_FILENO, "\x1b[2J\x1b[H", 7);
    exit(0);
}

static void leave_editor(int quit) {
    global_cfg.goto_prompt = 0;
    if (global_cfg.editing && hex_edit_dirty_count()) {
        global_cfg.edit_exit_prompt = quit ? 2 : 1;
        return;
    }
    if (quit)
        quit_editor();
    if (global_cfg.editing) {
        hex_edit_cancel();
        if (global_cfg.editing)
            return;
    }
}

static void edit_exit_keypress(int key) {
    int quit = global_cfg.edit_exit_prompt == 2;
    if (key == '\x1b') {
        global_cfg.edit_exit_prompt = 0;
        return;
    }
    if (key != 's' && key != 'S' && key != 'd' && key != 'D')
        return;
    /* Dismiss the prompt on an error so the failure message is visible. */
    global_cfg.edit_exit_prompt = 0;
    if ((key == 's' || key == 'S') && !hex_edit_save())
        return;
    if (quit)
        quit_editor();
    hex_edit_cancel();
}

static int hex_digit(int key) {
    if (key >= '0' && key <= '9') return key - '0';
    if (key >= 'a' && key <= 'f') return key - 'a' + 10;
    if (key >= 'A' && key <= 'F') return key - 'A' + 10;
    return -1;
}

static void open_goto_prompt(void) {
    global_cfg.goto_prompt = 1;
    global_cfg.goto_length = 0;
    global_cfg.goto_input[0] = '\0';
    global_cfg.edit_nibble = 0;
    global_cfg.statusmsg[0] = '\0';
}

static void goto_keypress(int key) {
    if (key == '\x1b') {
        global_cfg.goto_prompt = 0;
    } else if (key == 127 || key == CTRL_KEY('h')) {
        if (global_cfg.goto_length)
            global_cfg.goto_input[--global_cfg.goto_length] = '\0';
        global_cfg.statusmsg[0] = '\0';
    } else if (key == '\r' || key == '\n') {
        const char *input = global_cfg.goto_input;
        size_t start = input[0] == '0' && (input[1] == 'x' || input[1] == 'X') ? 2 : 0;
        size_t offset = 0;
        int valid = global_cfg.goto_length > start;
        for (size_t i = start; valid && i < global_cfg.goto_length; ++i) {
            int digit = hex_digit((unsigned char)input[i]);
            if (digit < 0 || offset > (SIZE_MAX - (size_t)digit) / 16)
                valid = 0;
            else
                offset = offset * 16 + (size_t)digit;
        }
        if (!valid || offset > global_cfg.num_bytes) {
            editor_set_status_message("Invalid offset; enter hex from 0 to %zx", global_cfg.num_bytes);
            return;
        }
        global_cfg.goto_prompt = 0;
        global_cfg.statusmsg[0] = '\0';
        global_cfg.cur_byte = offset;
        switch_mode();
    } else if (hex_digit(key) >= 0 || key == 'x' || key == 'X') {
        if (global_cfg.goto_length < sizeof(global_cfg.goto_input) - 1) {
            global_cfg.goto_input[global_cfg.goto_length++] = (char)key;
            global_cfg.goto_input[global_cfg.goto_length] = '\0';
            global_cfg.statusmsg[0] = '\0';
        } else {
            editor_set_status_message("Offset too long; Backspace to correct");
        }
    }
}

static void edit_keypress(int key) {
    switch (key) {
        case '\x1b':
        case F10_KEY:
            leave_editor(0);
            return;
        case F9_KEY:
            if (hex_edit_save())
                global_cfg.edit_nibble = 0;
            return;
        case F5_KEY:
            open_goto_prompt();
            return;
        case F7_KEY:
            search_open_prompt();
            return;
        case SHIFT_F7:
            search_repeat(global_cfg.search_backward);
            return;
        case F8_KEY:
            executable_browser_open();
            return;
        case '\t':
            global_cfg.edit_ascii = !global_cfg.edit_ascii;
            global_cfg.edit_nibble = 0;
            global_cfg.statusmsg[0] = '\0';
            return;
        case 127:
        case CTRL_KEY('h'):
            editor_move_cursor(ARROW_LEFT);
            return;
        case HOME_KEY:
        case END_KEY:
        case CTRL_HOME:
        case CTRL_END:
        case PAGE_UP:
        case PAGE_DOWN:
        case ARROW_UP:
        case ARROW_DOWN:
        case ARROW_LEFT:
        case ARROW_RIGHT:
            editor_move_cursor(key);
            return;
    }
    int digit = hex_digit(key);
    if ((global_cfg.edit_ascii && (key < 32 || key >= 127)) ||
        (!global_cfg.edit_ascii && digit < 0))
        return;
    size_t offset = global_cfg.cur_byte;
    if (offset >= global_cfg.num_bytes) {
        editor_set_status_message("End of file: overwrite existing bytes only");
        return;
    }
    if (!hex_edit_check_file())
        return;
    uint8_t byte = global_cfg.edit_ascii ? (uint8_t)key
        : global_cfg.edit_nibble
            ? (global_cfg.file[offset] & 0xf0) | (uint8_t)digit
            : (global_cfg.file[offset] & 0x0f) | (uint8_t)(digit << 4);
    if (!hex_edit_set_byte(offset, byte))
        return;
    global_cfg.statusmsg[0] = '\0';
    if (global_cfg.edit_ascii || global_cfg.edit_nibble) {
        global_cfg.cur_byte++;
        global_cfg.edit_nibble = 0;
        switch_mode();
    } else {
        global_cfg.edit_nibble = 1;
    }
}

void editor_process_keypress(void) {
    int c = editor_read_key();
    if (global_cfg.edit_exit_prompt) {
        edit_exit_keypress(c);
        return;
    }
    if (c == CTRL_KEY('q')) {
        leave_editor(1);
        return;
    }
    if (editor_update_window_size())
        editor_refresh_screen();
    if (!c || global_cfg.window_too_small)
        return;
    if (global_cfg.goto_prompt) {
        goto_keypress(c);
        return;
    }
    if (global_cfg.search_prompt) {
        search_keypress(c);
        return;
    }
    if (global_cfg.executable_browser) {
        executable_browser_keypress(c);
        return;
    }
    if (global_cfg.editing) {
        edit_keypress(c);
        return;
    }
    if (global_cfg.architecture_menu) {
        architecture_menu_keypress(c);
        return;
    }
    switch (c) {
        case F8_KEY:
        case 'b':
            executable_browser_open();
            break;
        case F3_KEY:
            if (hex_edit_begin()) {
                global_cfg.edit_ascii = 0;
                global_cfg.edit_nibble = 0;
                if (global_cfg.cur_byte == global_cfg.num_bytes && global_cfg.num_bytes)
                    global_cfg.cur_byte--;
                change_mode(HEX_MODE);
            }
            break;
        case F5_KEY:
        case 'g':
            open_goto_prompt();
            break;
        case F7_KEY:
        case 's':
            search_open_prompt();
            break;
        case SHIFT_F7:
            search_repeat(global_cfg.search_backward);
            break;
        case 'n':
            search_repeat(0);
            break;
        case 'N':
            search_repeat(1);
            break;
        case CTRL_KEY('m'):
            change_mode(global_cfg.mode == 0 ? DISASSEMBLER_MODE : global_cfg.mode - 1);
            break;
        case 'm':
            change_mode((global_cfg.mode + 1) % 3);
            break;
        case SHIFT_F1:
        case 'a':
            global_cfg.architecture_menu = 1;
            global_cfg.architecture_choice = architecture_current_profile();
            break;
        case 'e': {
            const binaryInfo *info = architecture_binary_info();
            if (info && info->has_entry) {
                architecture_jump_to_entry();
                change_mode(DISASSEMBLER_MODE);
            } else {
                editor_set_status_message("No entry point in this file");
            }
            break;
        }
        case 'o': {
            if (global_cfg.architecture != ARCH_X86) {
                editor_set_status_message("Use a / Shift-F1 to select architecture");
                break;
            }
            disassemblerMode next = (global_cfg.disassembler_mode + 1) % 4;
            for (size_t index = 1; index < architecture_profile_count(); ++index) {
                const architectureProfile *profile = architecture_profile(index);
                if (profile->spec.id == ARCH_X86 && profile->spec.x86_mode == next) {
                    architecture_select(index);
                    break;
                }
            }
            break;
        }
        case HOME_KEY:
        case END_KEY:
        case CTRL_HOME:
        case CTRL_END:
        case PAGE_UP:
        case PAGE_DOWN:
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
