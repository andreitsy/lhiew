#include "lhiew/types.h"
#include "test_harness.h"
#include "lhiew/disassembler.h"

static void setup_with_bytes(const uint8_t *data, size_t len) {
    RESET_GLOBAL_CFG();
    global_cfg.num_bytes = len;
    if (len) {
        global_cfg.file = malloc(len);
        memcpy(global_cfg.file, data, len);
    }

    global_cfg.cur_screencols = 80;
    global_cfg.screencols = 80;
    global_cfg.screenrows = 10;
    global_cfg.disassembler_mode = MODE_LONG_COMPAT_64;
    global_cfg.disassembler_buffer = calloc(global_cfg.screenrows, sizeof(disassemblerRow));
}

static void teardown(void) {
    free_disassembler_buffer();
    free(global_cfg.file);
    global_cfg.file = NULL;
}

static void test_disassemble_nops(void) {
    /* 0x90 = NOP in x86 */
    uint8_t nops[16];
    memset(nops, 0x90, sizeof(nops));
    setup_with_bytes(nops, sizeof(nops));

    int ret = disassemble_block(0);
    ASSERT_EQ(ret, EXIT_SUCCESS);

    /* First row should start at byte 0 */
    ASSERT_EQ(global_cfg.disassembler_buffer[0].start_byte, (size_t)0);
    /* NOP is 1 byte */
    ASSERT_EQ(global_cfg.disassembler_buffer[0].end_byte, (size_t)1);
    /* The disassembled string should contain "nop" */
    ASSERT(strstr(global_cfg.disassembler_buffer[0].diss_str, "nop") != NULL);

    teardown();
}

static void test_disassemble_fills_rows(void) {
    /* Fill with NOPs — should get screenrows entries */
    uint8_t nops[64];
    memset(nops, 0x90, sizeof(nops));
    setup_with_bytes(nops, sizeof(nops));

    disassemble_block(0);

    for (size_t i = 0; i < global_cfg.screenrows; i++) {
        ASSERT_EQ(global_cfg.disassembler_buffer[i].start_byte, i);
        ASSERT_EQ(global_cfg.disassembler_buffer[i].end_byte, i + 1);
    }

    teardown();
}

static void test_disassemble_32bit_mode(void) {
    uint8_t nops[16];
    memset(nops, 0x90, sizeof(nops));
    setup_with_bytes(nops, sizeof(nops));
    global_cfg.disassembler_mode = MODE_LONG_COMPAT_32;

    int ret = disassemble_block(0);
    ASSERT_EQ(ret, EXIT_SUCCESS);
    ASSERT(strstr(global_cfg.disassembler_buffer[0].diss_str, "nop") != NULL);

    teardown();
}

static void test_disassemble_16bit_mode(void) {
    uint8_t nops[16];
    memset(nops, 0x90, sizeof(nops));
    setup_with_bytes(nops, sizeof(nops));
    global_cfg.disassembler_mode = MODE_LONG_COMPAT_16;

    int ret = disassemble_block(0);
    ASSERT_EQ(ret, EXIT_SUCCESS);
    ASSERT(strstr(global_cfg.disassembler_buffer[0].diss_str, "nop") != NULL);

    teardown();
}

static void test_disassemble_real_mode(void) {
    uint8_t nops[16];
    memset(nops, 0x90, sizeof(nops));
    setup_with_bytes(nops, sizeof(nops));
    global_cfg.disassembler_mode = REAL;

    int ret = disassemble_block(0);
    ASSERT_EQ(ret, EXIT_SUCCESS);
    ASSERT(strstr(global_cfg.disassembler_buffer[0].diss_str, "nop") != NULL);

    teardown();
}

static void test_disassemble_ret_instruction(void) {
    /* 0xC3 = RET in x86 */
    uint8_t code[16];
    memset(code, 0xC3, sizeof(code));
    setup_with_bytes(code, sizeof(code));

    disassemble_block(0);

    ASSERT(strstr(global_cfg.disassembler_buffer[0].diss_str, "ret") != NULL);
    ASSERT_EQ(global_cfg.disassembler_buffer[0].end_byte, (size_t)1);

    teardown();
}

static void test_disassemble_from_offset(void) {
    /* NOPs followed by RET */
    uint8_t code[16];
    memset(code, 0x90, sizeof(code));
    code[5] = 0xC3;
    setup_with_bytes(code, sizeof(code));

    disassemble_block(5);

    ASSERT_EQ(global_cfg.disassembler_buffer[0].start_byte, (size_t)5);
    ASSERT(strstr(global_cfg.disassembler_buffer[0].diss_str, "ret") != NULL);

    teardown();
}

static void test_disassemble_one_row_with_lookback(void) {
    uint8_t code[300];
    memset(code, 0x90, sizeof(code));
    code[200] = 0xC3;
    setup_with_bytes(code, sizeof(code));
    global_cfg.screenrows = 1;

    ASSERT_EQ(disassemble_block(200), EXIT_SUCCESS);
    ASSERT_EQ(global_cfg.disassembler_buffer[0].start_byte, (size_t)200);
    ASSERT_EQ(global_cfg.disassembler_buffer[0].end_byte, (size_t)201);
    ASSERT(strstr(global_cfg.disassembler_buffer[0].diss_str, "ret") != NULL);

    teardown();
}

static void test_disassemble_clears_rows_at_end_of_file(void) {
    uint8_t code[16];
    memset(code, 0x90, sizeof(code));
    setup_with_bytes(code, sizeof(code));

    ASSERT_EQ(disassemble_block(0), EXIT_SUCCESS);
    ASSERT_EQ(disassemble_block(sizeof(code) - 1), EXIT_SUCCESS);
    ASSERT_EQ(global_cfg.disassembler_buffer[0].start_byte, sizeof(code) - 1);
    ASSERT_EQ(global_cfg.disassembler_buffer[0].end_byte, sizeof(code));
    for (size_t i = 1; i < global_cfg.screenrows; ++i) {
        ASSERT_EQ(global_cfg.disassembler_buffer[i].start_byte, (size_t)0);
        ASSERT_EQ(global_cfg.disassembler_buffer[i].end_byte, (size_t)0);
        ASSERT_STR_EQ(global_cfg.disassembler_buffer[i].diss_str, "");
    }

    ASSERT_EQ(disassemble_block(sizeof(code)), EXIT_SUCCESS);
    ASSERT_EQ(global_cfg.disassembler_buffer[0].end_byte, (size_t)0);
    ASSERT_STR_EQ(global_cfg.disassembler_buffer[0].diss_str, "");
    ASSERT_EQ(disassemble_block(SIZE_MAX), EXIT_SUCCESS);
    ASSERT_EQ(global_cfg.disassembler_buffer[0].end_byte, (size_t)0);

    teardown();
}

static void test_disassemble_cursor_inside_instruction_after_resize(void) {
    const uint8_t code[] = {0x48, 0x89, 0xD8, 0x90, 0xC3};
    setup_with_bytes(code, sizeof(code));
    global_cfg.screenrows = 1;

    ASSERT_EQ(disassemble_block(0), EXIT_SUCCESS);
    ASSERT_EQ(global_cfg.disassembler_buffer[0].end_byte, (size_t)3);
    /* Rows beyond the former viewport are initialized when it grows. */
    global_cfg.screenrows = 10;
    ASSERT_EQ(disassemble_block(2), EXIT_SUCCESS);
    ASSERT_EQ(global_cfg.disassembler_buffer[0].start_byte, (size_t)0);
    ASSERT_EQ(global_cfg.disassembler_buffer[0].end_byte, (size_t)3);
    ASSERT(strstr(global_cfg.disassembler_buffer[0].diss_str, "mov") != NULL);
    ASSERT_EQ(global_cfg.disassembler_buffer[1].start_byte, (size_t)3);
    ASSERT_EQ(global_cfg.disassembler_buffer[2].start_byte, (size_t)4);
    ASSERT_EQ(global_cfg.disassembler_buffer[3].end_byte, (size_t)0);

    teardown();
}

static void test_disassemble_invalid_and_truncated_bytes(void) {
    const uint8_t code[] = {0x06, 0x90, 0xE8};
    setup_with_bytes(code, sizeof(code));

    ASSERT_EQ(disassemble_block(0), EXIT_SUCCESS);
    ASSERT_EQ(global_cfg.disassembler_buffer[0].start_byte, (size_t)0);
    ASSERT_EQ(global_cfg.disassembler_buffer[0].end_byte, (size_t)1);
    ASSERT_STR_EQ(global_cfg.disassembler_buffer[0].diss_str, "db 06");
    ASSERT(strstr(global_cfg.disassembler_buffer[1].diss_str, "nop") != NULL);
    ASSERT_EQ(global_cfg.disassembler_buffer[2].start_byte, (size_t)2);
    ASSERT_EQ(global_cfg.disassembler_buffer[2].end_byte, (size_t)3);
    ASSERT_STR_EQ(global_cfg.disassembler_buffer[2].diss_str, "db E8");
    ASSERT_EQ(global_cfg.disassembler_buffer[3].end_byte, (size_t)0);

    teardown();
}

static void test_disassemble_tall_window(void) {
    const uint8_t code[] = {0x90, 0xC3};
    setup_with_bytes(code, sizeof(code));
    free_disassembler_buffer();
    global_cfg.screenrows = 65535;
    global_cfg.disassembler_buffer = calloc(global_cfg.screenrows, sizeof(disassemblerRow));
    ASSERT_NE(global_cfg.disassembler_buffer, NULL);

    ASSERT_EQ(disassemble_block(0), EXIT_SUCCESS);
    ASSERT_EQ(global_cfg.disassembler_buffer[0].end_byte, (size_t)1);
    ASSERT_EQ(global_cfg.disassembler_buffer[1].end_byte, (size_t)2);
    for (size_t i = 2; i < global_cfg.screenrows; ++i) {
        ASSERT_EQ(global_cfg.disassembler_buffer[i].end_byte, (size_t)0);
        ASSERT_STR_EQ(global_cfg.disassembler_buffer[i].diss_str, "");
    }

    teardown();
}

static void test_disassemble_empty_or_missing_file(void) {
    setup_with_bytes(NULL, 0);
    global_cfg.disassembler_buffer[0].end_byte = 1;
    strcpy(global_cfg.disassembler_buffer[0].diss_str, "stale instruction");

    ASSERT_EQ(disassemble_block(0), EXIT_SUCCESS);
    ASSERT_EQ(global_cfg.disassembler_buffer[0].end_byte, (size_t)0);
    ASSERT_STR_EQ(global_cfg.disassembler_buffer[0].diss_str, "");
    global_cfg.num_bytes = 10;
    ASSERT_EQ(disassemble_block(0), EXIT_SUCCESS);
    ASSERT_EQ(global_cfg.disassembler_buffer[0].end_byte, (size_t)0);

    teardown();
}

static void test_disassemble_without_visible_rows_or_buffer(void) {
    const uint8_t code[] = {0x90};
    setup_with_bytes(code, sizeof(code));
    global_cfg.screenrows = 0;
    ASSERT_EQ(disassemble_block(0), EXIT_SUCCESS);
    free_disassembler_buffer();
    global_cfg.screenrows = 10;
    ASSERT_EQ(disassemble_block(0), EXIT_SUCCESS);

    teardown();
}

static void test_free_disassembler_buffer_null_safe(void) {
    RESET_GLOBAL_CFG();
    global_cfg.disassembler_buffer = NULL;
    /* free(NULL) is safe per C standard */
    free_disassembler_buffer();
    ASSERT_EQ(global_cfg.disassembler_buffer, NULL);
}

int main(void) {
    printf("test_disassembler:\n");
    RUN_TEST(test_disassemble_nops);
    RUN_TEST(test_disassemble_fills_rows);
    RUN_TEST(test_disassemble_32bit_mode);
    RUN_TEST(test_disassemble_16bit_mode);
    RUN_TEST(test_disassemble_real_mode);
    RUN_TEST(test_disassemble_ret_instruction);
    RUN_TEST(test_disassemble_from_offset);
    RUN_TEST(test_disassemble_one_row_with_lookback);
    RUN_TEST(test_disassemble_clears_rows_at_end_of_file);
    RUN_TEST(test_disassemble_cursor_inside_instruction_after_resize);
    RUN_TEST(test_disassemble_invalid_and_truncated_bytes);
    RUN_TEST(test_disassemble_tall_window);
    RUN_TEST(test_disassemble_empty_or_missing_file);
    RUN_TEST(test_disassemble_without_visible_rows_or_buffer);
    RUN_TEST(test_free_disassembler_buffer_null_safe);
    TEST_REPORT();
}
