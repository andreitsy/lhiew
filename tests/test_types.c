#include "test_harness.h"
#include "lhiew/types.h"

static void test_editor_mode_values(void) {
    ASSERT_EQ(TEXT_MODE, 0);
    ASSERT_EQ(HEX_MODE, 1);
    ASSERT_EQ(DISASSEMBLER_MODE, 2);
}

static void test_disassembler_mode_values(void) {
    ASSERT_EQ(REAL, 0);
    ASSERT_EQ(MODE_LONG_COMPAT_16, 1);
    ASSERT_EQ(MODE_LONG_COMPAT_32, 2);
    ASSERT_EQ(MODE_LONG_COMPAT_64, 3);
}

static void test_constants(void) {
    ASSERT_EQ(HEX_BYTE_LENGTH, 16);
    ASSERT_EQ(DISASSEMBLED_BUFFER_SIZE, 128);
    ASSERT_EQ(SCREENCOLS_MIN, 80);
}

static void test_editor_config_zero_init(void) {
    RESET_GLOBAL_CFG();
    ASSERT_EQ(global_cfg.cx, (size_t)0);
    ASSERT_EQ(global_cfg.cy, (size_t)0);
    ASSERT_EQ(global_cfg.cur_byte, (size_t)0);
    ASSERT_EQ(global_cfg.num_bytes, (size_t)0);
    ASSERT_EQ(global_cfg.file, NULL);
    ASSERT_EQ(global_cfg.filename, NULL);
    ASSERT_EQ(global_cfg.fp, NULL);
    ASSERT_EQ(global_cfg.mode, TEXT_MODE);
}

static void test_disassembler_row_size(void) {
    ASSERT_GT(sizeof(disassemblerRow), (size_t)0);
    /* diss_str should hold DISASSEMBLED_BUFFER_SIZE chars */
    disassemblerRow row;
    ASSERT_EQ(sizeof(row.diss_str), (size_t)DISASSEMBLED_BUFFER_SIZE);
}

static void test_mode_cycling(void) {
    /* Verify mode enum wraps correctly with % 3 */
    editorMode m = TEXT_MODE;
    m = (m + 1) % 3;
    ASSERT_EQ(m, HEX_MODE);
    m = (m + 1) % 3;
    ASSERT_EQ(m, DISASSEMBLER_MODE);
    m = (m + 1) % 3;
    ASSERT_EQ(m, TEXT_MODE);
}

static void test_disassembler_mode_cycling(void) {
    disassemblerMode dm = REAL;
    dm = (dm + 1) % 4;
    ASSERT_EQ(dm, MODE_LONG_COMPAT_16);
    dm = (dm + 1) % 4;
    ASSERT_EQ(dm, MODE_LONG_COMPAT_32);
    dm = (dm + 1) % 4;
    ASSERT_EQ(dm, MODE_LONG_COMPAT_64);
    dm = (dm + 1) % 4;
    ASSERT_EQ(dm, REAL);
}

int main(void) {
    printf("test_types:\n");
    RUN_TEST(test_editor_mode_values);
    RUN_TEST(test_disassembler_mode_values);
    RUN_TEST(test_constants);
    RUN_TEST(test_editor_config_zero_init);
    RUN_TEST(test_disassembler_row_size);
    RUN_TEST(test_mode_cycling);
    RUN_TEST(test_disassembler_mode_cycling);
    TEST_REPORT();
}
