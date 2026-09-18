#include "lhiew/types.h"
#include "test_harness.h"
#include "lhiew/editor.h"

static void test_switch_mode_text(void) {
    RESET_GLOBAL_CFG();
    global_cfg.screencols = 80;
    global_cfg.cur_screencols = 80;
    global_cfg.num_bytes = 320;
    global_cfg.cur_byte = 85;
    global_cfg.mode = TEXT_MODE;

    switch_mode();

    ASSERT_EQ(global_cfg.cur_screencols, (size_t)80);
    ASSERT_EQ(global_cfg.cy, (size_t)1);   /* 85/80 = 1 */
    ASSERT_EQ(global_cfg.cx, (size_t)5);   /* 85%80 = 5 */
    ASSERT_EQ(global_cfg.numrows, (size_t)5); /* 320/80+1 = 5 */
}

static void test_switch_mode_hex(void) {
    RESET_GLOBAL_CFG();
    global_cfg.screencols = 80;
    global_cfg.cur_screencols = 80;
    global_cfg.num_bytes = 256;
    global_cfg.cur_byte = 33;
    global_cfg.mode = HEX_MODE;

    switch_mode();

    /* An 80-column terminal fits sixteen hex bytes, offsets and ASCII. */
    ASSERT_EQ(global_cfg.cur_screencols, (size_t)16);
    ASSERT_EQ(global_cfg.cy, (size_t)2);   /* 33/16 = 2 */
    ASSERT_EQ(global_cfg.cx, (size_t)1);   /* 33%16 = 1 */
    ASSERT_EQ(global_cfg.numrows, (size_t)17); /* 256/16+1 = 17 */
}

static void test_switch_mode_disassembler(void) {
    RESET_GLOBAL_CFG();
    global_cfg.screencols = 80;
    global_cfg.cur_screencols = 16;
    global_cfg.num_bytes = 100;
    global_cfg.cur_byte = 10;
    global_cfg.mode = DISASSEMBLER_MODE;

    switch_mode();

    ASSERT_EQ(global_cfg.cur_screencols, (size_t)80);
    ASSERT_EQ(global_cfg.cy, (size_t)0);   /* 10/80 = 0 */
    ASSERT_EQ(global_cfg.cx, (size_t)10);  /* 10%80 = 10 */
}

static void test_switch_mode_preserves_cur_byte(void) {
    RESET_GLOBAL_CFG();
    global_cfg.screencols = 80;
    global_cfg.num_bytes = 1000;
    global_cfg.cur_byte = 500;

    global_cfg.mode = TEXT_MODE;
    switch_mode();
    size_t reconstructed = global_cfg.cy * global_cfg.cur_screencols + global_cfg.cx;
    ASSERT_EQ(reconstructed, (size_t)500);

    global_cfg.mode = HEX_MODE;
    switch_mode();
    reconstructed = global_cfg.cy * global_cfg.cur_screencols + global_cfg.cx;
    ASSERT_EQ(reconstructed, (size_t)500);
}

static void test_resize_preserves_position_and_reflows(void) {
    RESET_GLOBAL_CFG();
    global_cfg.num_bytes = 1000;
    global_cfg.cur_byte = 517;
    editor_resize(24, 80);
    ASSERT_EQ(global_cfg.screenrows, (size_t)22);
    ASSERT_EQ(global_cfg.cur_screencols, (size_t)80);
    ASSERT_EQ(global_cfg.window_too_small, 0);

    editor_resize(8, 31);
    ASSERT_EQ(global_cfg.terminal_rows, (size_t)8);
    ASSERT_EQ(global_cfg.screenrows, (size_t)6);
    ASSERT_EQ(global_cfg.screencols, (size_t)31);
    ASSERT_EQ(global_cfg.cur_byte, (size_t)517);
    ASSERT_EQ(global_cfg.cy, (size_t)16);
    ASSERT_EQ(global_cfg.cx, (size_t)21);

    editor_resize(50, 160);
    ASSERT_EQ(global_cfg.screenrows, (size_t)48);
    ASSERT_EQ(global_cfg.cur_byte, (size_t)517);
    ASSERT_EQ(global_cfg.cy, (size_t)3);
    ASSERT_EQ(global_cfg.cx, (size_t)37);
    /* Rows added after shrinking must not contain old decoded instructions. */
    ASSERT_EQ(global_cfg.disassembler_buffer[47].end_byte, (size_t)0);
    free(global_cfg.disassembler_buffer);
}

static void test_resize_too_small_and_recovery(void) {
    RESET_GLOBAL_CFG();
    global_cfg.num_bytes = 100;
    global_cfg.cur_byte = 91;
    editor_resize(5, 24);
    ASSERT_EQ(global_cfg.window_too_small, 0);
    ASSERT_EQ(global_cfg.screenrows, (size_t)3);
    editor_resize(4, 24);
    ASSERT_EQ(global_cfg.window_too_small, 1);
    editor_resize(5, 23);
    ASSERT_EQ(global_cfg.window_too_small, 1);
    editor_resize(1, 1);
    ASSERT_EQ(global_cfg.screenrows, (size_t)0);
    ASSERT_EQ(global_cfg.disassembler_buffer, NULL);
    editor_resize(0, 0);
    ASSERT_EQ(global_cfg.window_too_small, 1);
    ASSERT_EQ(global_cfg.cur_byte, (size_t)91);
    ASSERT_GT(global_cfg.cur_screencols, (size_t)0);
    editor_resize(5, 24);
    ASSERT_EQ(global_cfg.window_too_small, 0);
    ASSERT_EQ(global_cfg.cur_byte, (size_t)91);
    ASSERT_EQ(global_cfg.cy * global_cfg.cur_screencols + global_cfg.cx, (size_t)91);
    free(global_cfg.disassembler_buffer);
}

static void test_hex_width_fits_terminal(void) {
    const size_t widths[] = {24, 31, 40, 60, 80, 120, 256};
    RESET_GLOBAL_CFG();
    global_cfg.mode = HEX_MODE;
    global_cfg.num_bytes = 1000;
    global_cfg.cur_byte = 99;
    for (size_t i = 0; i < sizeof(widths) / sizeof(widths[0]); i++) {
        editor_resize(12, widths[i]);
        ASSERT(global_cfg.cur_screencols * 4 + editor_offset_width() + 5 <= widths[i]);
        ASSERT_EQ(global_cfg.cur_byte, (size_t)99);
        ASSERT_EQ(global_cfg.cy * global_cfg.cur_screencols + global_cfg.cx, (size_t)99);
    }
    ASSERT_GT(global_cfg.cur_screencols, (size_t)16);
    free(global_cfg.disassembler_buffer);
}

static void test_switch_mode_clamps_cursor_and_scroll_without_overflow(void) {
    RESET_GLOBAL_CFG();
    global_cfg.num_bytes = 100;
    global_cfg.cur_byte = SIZE_MAX;
    global_cfg.rowoff = SIZE_MAX;
    global_cfg.cur_screencols = 80;
    global_cfg.screencols = 24;
    switch_mode();
    ASSERT_EQ(global_cfg.cur_byte, (size_t)100);
    ASSERT_EQ(global_cfg.cy, (size_t)4);
    ASSERT_EQ(global_cfg.cx, (size_t)4);
    ASSERT_EQ(global_cfg.rowoff, (size_t)4);

    global_cfg.num_bytes = SIZE_MAX;
    global_cfg.cur_byte = SIZE_MAX;
    global_cfg.screencols = 0;
    switch_mode();
    ASSERT_EQ(global_cfg.cur_screencols, (size_t)1);
    ASSERT_EQ(global_cfg.numrows, SIZE_MAX);
    ASSERT_EQ(global_cfg.cy, SIZE_MAX);
    ASSERT_EQ(global_cfg.cx, (size_t)0);

    global_cfg.num_bytes = 0;
    switch_mode();
    ASSERT_EQ(global_cfg.cur_byte, (size_t)0);
    ASSERT_EQ(global_cfg.numrows, (size_t)0);
    ASSERT_EQ(global_cfg.cy, (size_t)0);
    ASSERT_EQ(global_cfg.cx, (size_t)0);
}

int main(void) {
    printf("test_editor:\n");
    RUN_TEST(test_switch_mode_text);
    RUN_TEST(test_switch_mode_hex);
    RUN_TEST(test_switch_mode_disassembler);
    RUN_TEST(test_switch_mode_preserves_cur_byte);
    RUN_TEST(test_resize_preserves_position_and_reflows);
    RUN_TEST(test_resize_too_small_and_recovery);
    RUN_TEST(test_hex_width_fits_terminal);
    RUN_TEST(test_switch_mode_clamps_cursor_and_scroll_without_overflow);
    TEST_REPORT();
}
