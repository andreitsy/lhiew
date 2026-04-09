#include "test_harness.h"
#include "lhiew/editor.h"
#include "lhiew/types.h"

static void test_get_row_len_empty(void) {
    RESET_GLOBAL_CFG();
    /* num_bytes == 0 → returns 0 */
    ASSERT_EQ(get_row_len(), (size_t)0);
}

static void test_get_row_len_full_row(void) {
    RESET_GLOBAL_CFG();
    global_cfg.num_bytes = 160;
    global_cfg.cur_screencols = 80;
    global_cfg.cy = 0;
    /* row 0: 160 - 0*80 = 160; min(160, 80) = 80 */
    ASSERT_EQ(get_row_len(), (size_t)80);
}

static void test_get_row_len_partial_row(void) {
    RESET_GLOBAL_CFG();
    global_cfg.num_bytes = 100;
    global_cfg.cur_screencols = 80;
    global_cfg.cy = 1;
    /* row 1: 100 - 1*80 = 20; min(20, 80) = 20 */
    ASSERT_EQ(get_row_len(), (size_t)20);
}

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

    ASSERT_EQ(global_cfg.cur_screencols, (size_t)HEX_BYTE_LENGTH);
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

int main(void) {
    printf("test_editor:\n");
    RUN_TEST(test_get_row_len_empty);
    RUN_TEST(test_get_row_len_full_row);
    RUN_TEST(test_get_row_len_partial_row);
    RUN_TEST(test_switch_mode_text);
    RUN_TEST(test_switch_mode_hex);
    RUN_TEST(test_switch_mode_disassembler);
    RUN_TEST(test_switch_mode_preserves_cur_byte);
    TEST_REPORT();
}
