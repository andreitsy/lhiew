#include "lhiew/types.h"
#include "lhiew/input.h"
#include "lhiew/architecture.h"
#include "lhiew/disassembler.h"
#include "lhiew/editor.h"
#include "lhiew/render.h"
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
            case PAGE_UP:
            case PAGE_DOWN: {
                size_t distance = global_cfg.screenrows * width;
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
    if (global_cfg.architecture_menu) {
        architecture_menu_keypress(c);
        return;
    }
    switch (c) {
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
