#include "lhiew/editor.h"
#include "lhiew/disassembler.h"
#include "lhiew/terminal.h"
#include "lhiew/types.h"

#include <stdlib.h>

size_t get_row_len(void) {
    if (global_cfg.num_bytes) {
        size_t tmp_len = global_cfg.num_bytes - global_cfg.cy * global_cfg.cur_screencols;
        return tmp_len < global_cfg.cur_screencols ? tmp_len : global_cfg.cur_screencols;
    }
    return 0;
}

void switch_mode(void) {
    switch (global_cfg.mode) {
        case TEXT_MODE:
            global_cfg.cur_screencols = global_cfg.screencols;
            break;
        case HEX_MODE:
            global_cfg.cur_screencols = HEX_BYTE_LENGTH;
            break;
        case DISASSEMBLER_MODE:
            global_cfg.cur_screencols = global_cfg.screencols;
            break;
    }
    global_cfg.cy = global_cfg.cur_byte / global_cfg.cur_screencols;
    global_cfg.cx = global_cfg.cur_byte % global_cfg.cur_screencols;
    global_cfg.numrows = global_cfg.num_bytes / global_cfg.cur_screencols + 1;
}

void init_editor(void) {
    global_cfg.disassembler_mode = MODE_LONG_COMPAT_32;
    global_cfg.mode = TEXT_MODE;
    global_cfg.cx = 0;
    global_cfg.cy = 0;
    global_cfg.rx = 0;
    global_cfg.num_bytes = 0;
    global_cfg.cur_byte = 0;
    global_cfg.numrows = 0;
    global_cfg.rowoff = 0;
    global_cfg.filename = NULL;
    global_cfg.fp = NULL;
    global_cfg.statusmsg[0] = '\0';
    global_cfg.statusmsg_time = 0;

    if (get_window_size(&global_cfg.screenrows,
                        &global_cfg.cur_screencols) == -1) {
        die_safely("getWindowSize");
    }
    if (global_cfg.screenrows >= 2)
        global_cfg.screenrows -= 2;
    global_cfg.cur_screencols = SCREENCOLS_MIN;
    global_cfg.screencols = global_cfg.cur_screencols;
    global_cfg.disassembler_buffer =
        malloc(global_cfg.screenrows * sizeof(disassemblerRow));
    atexit(free_disassembler_buffer);
}
