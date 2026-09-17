#include "lhiew/types.h"
#include "test_harness.h"
#include "lhiew/input.h"
#include "lhiew/disassembler.h"
#include "lhiew/editor.h"
#include "lhiew/terminal.h"

#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

static const char *TMP_FILE = "/tmp/lhiew_test_input.bin";

static void setup_text_mode(size_t nbytes, size_t cols) {
    RESET_GLOBAL_CFG();

    /* Create a temp file with nbytes of data */
    FILE *f = fopen(TMP_FILE, "wb");
    for (size_t i = 0; i < nbytes; i++) {
        uint8_t b = (uint8_t)(i & 0xFF);
        fwrite(&b, 1, 1, f);
    }
    fclose(f);

    global_cfg.fp = fopen(TMP_FILE, "rb");
    global_cfg.filename = strdup(TMP_FILE);
    int fd = fileno(global_cfg.fp);
    struct stat st;
    fstat(fd, &st);
    global_cfg.num_bytes = st.st_size;
    global_cfg.file = mmap(NULL, global_cfg.num_bytes, PROT_READ, MAP_PRIVATE, fd, 0);

    global_cfg.mode = TEXT_MODE;
    global_cfg.cur_screencols = cols;
    global_cfg.screencols = cols;
    global_cfg.numrows = global_cfg.num_bytes / cols + 1;
    global_cfg.screenrows = 24;
    global_cfg.cx = 0;
    global_cfg.cy = 0;
    global_cfg.cur_byte = 0;
}

static void teardown(void) {
    if (global_cfg.fp) { fclose(global_cfg.fp); global_cfg.fp = NULL; }
    free(global_cfg.filename);
    global_cfg.filename = NULL;
    unlink(TMP_FILE);
}

static void test_move_right(void) {
    setup_text_mode(256, 16);
    editor_move_cursor(ARROW_RIGHT);
    ASSERT_EQ(global_cfg.cx, (size_t)1);
    ASSERT_EQ(global_cfg.cy, (size_t)0);
    ASSERT_EQ(global_cfg.cur_byte, (size_t)1);
    teardown();
}

static void test_move_left_at_origin(void) {
    setup_text_mode(256, 16);
    editor_move_cursor(ARROW_LEFT);
    /* Should stay at 0 */
    ASSERT_EQ(global_cfg.cx, (size_t)0);
    ASSERT_EQ(global_cfg.cy, (size_t)0);
    ASSERT_EQ(global_cfg.cur_byte, (size_t)0);
    teardown();
}

static void test_move_down(void) {
    setup_text_mode(256, 16);
    editor_move_cursor(ARROW_DOWN);
    ASSERT_EQ(global_cfg.cy, (size_t)1);
    ASSERT_EQ(global_cfg.cur_byte, (size_t)16);
    teardown();
}

static void test_move_up_from_row1(void) {
    setup_text_mode(256, 16);
    global_cfg.cy = 1;
    global_cfg.cur_byte = 16;
    editor_move_cursor(ARROW_UP);
    ASSERT_EQ(global_cfg.cy, (size_t)0);
    ASSERT_EQ(global_cfg.cur_byte, (size_t)0);
    teardown();
}

static void test_vim_keys(void) {
    setup_text_mode(256, 16);
    editor_move_cursor('l');
    ASSERT_EQ(global_cfg.cur_byte, (size_t)1);
    editor_move_cursor('j');
    ASSERT_EQ(global_cfg.cur_byte, (size_t)17);
    editor_move_cursor('h');
    ASSERT_EQ(global_cfg.cur_byte, (size_t)16);
    editor_move_cursor('k');
    ASSERT_EQ(global_cfg.cur_byte, (size_t)0);
    teardown();
}

static void test_move_right_wraps_row(void) {
    setup_text_mode(256, 4);
    global_cfg.cx = 3;
    global_cfg.cy = 0;
    global_cfg.cur_byte = 3;
    editor_move_cursor(ARROW_RIGHT);
    /* Should wrap to next row */
    ASSERT_EQ(global_cfg.cy, (size_t)1);
    ASSERT_EQ(global_cfg.cx, (size_t)0);
    ASSERT_EQ(global_cfg.cur_byte, (size_t)4);
    teardown();
}

static void test_hex_mode_cursor(void) {
    setup_text_mode(256, 80);
    global_cfg.mode = HEX_MODE;
    global_cfg.cur_screencols = HEX_BYTE_LENGTH;
    global_cfg.numrows = 256 / HEX_BYTE_LENGTH + 1;

    editor_move_cursor(ARROW_RIGHT);
    ASSERT_EQ(global_cfg.cur_byte, (size_t)1);
    ASSERT_EQ(global_cfg.cx, (size_t)1);

    editor_move_cursor(ARROW_DOWN);
    ASSERT_EQ(global_cfg.cur_byte, (size_t)17);
    ASSERT_EQ(global_cfg.cy, (size_t)1);

    teardown();
}

static void test_ctrl_key_macro(void) {
    ASSERT_EQ(CTRL_KEY('q'), 17);
    ASSERT_EQ(CTRL_KEY('m'), 13);
    ASSERT_EQ(CTRL_KEY('a'), 1);
}

static void test_partial_row_navigation_after_resize(void) {
    setup_text_mode(101, 80);
    global_cfg.cur_byte = 99;
    editor_resize(5, 24);
    editor_move_cursor(ARROW_DOWN);
    ASSERT_EQ(global_cfg.cur_byte, (size_t)101);
    editor_move_cursor(ARROW_RIGHT);
    ASSERT_EQ(global_cfg.cur_byte, (size_t)101);
    editor_move_cursor(ARROW_LEFT);
    ASSERT_EQ(global_cfg.cur_byte, (size_t)100);
    global_cfg.cur_byte = 96;
    switch_mode();
    editor_move_cursor(ARROW_LEFT);
    ASSERT_EQ(global_cfg.cur_byte, (size_t)95);
    ASSERT_EQ(global_cfg.cx, (size_t)23);
    ASSERT_EQ(global_cfg.cy, (size_t)3);
    free(global_cfg.disassembler_buffer);
    teardown();
}

static void test_navigation_pauses_while_too_small(void) {
    setup_text_mode(256, 80);
    global_cfg.cur_byte = 45;
    editor_resize(2, 10);
    editor_move_cursor(ARROW_DOWN);
    ASSERT_EQ(global_cfg.cur_byte, (size_t)45);
    editor_resize(8, 40);
    editor_move_cursor(ARROW_RIGHT);
    ASSERT_EQ(global_cfg.cur_byte, (size_t)46);
    free(global_cfg.disassembler_buffer);
    teardown();
}

static void test_disassembler_navigation_after_resize_and_eof(void) {
    /* push %rbp; mov %rsp, %rbp; nop; ret */
    uint8_t code[] = {0x55, 0x48, 0x89, 0xe5, 0x90, 0xc3};
    RESET_GLOBAL_CFG();
    global_cfg.file = code;
    global_cfg.num_bytes = sizeof(code);
    global_cfg.mode = DISASSEMBLER_MODE;
    global_cfg.disassembler_mode = MODE_LONG_COMPAT_64;
    editor_resize(24, 80);
    editor_move_cursor(ARROW_DOWN);
    ASSERT_EQ(global_cfg.cur_byte, (size_t)1);
    editor_move_cursor(ARROW_RIGHT);
    ASSERT_EQ(global_cfg.cur_byte, (size_t)2);
    editor_resize(5, 24);
    editor_move_cursor(ARROW_DOWN);
    ASSERT_EQ(global_cfg.cur_byte, (size_t)4);
    editor_move_cursor(ARROW_DOWN);
    ASSERT_EQ(global_cfg.cur_byte, (size_t)5);
    editor_move_cursor(ARROW_DOWN);
    ASSERT_EQ(global_cfg.cur_byte, sizeof(code));
    editor_move_cursor(ARROW_RIGHT);
    editor_move_cursor(ARROW_DOWN);
    ASSERT_EQ(global_cfg.cur_byte, sizeof(code));
    editor_move_cursor(ARROW_LEFT);
    ASSERT_EQ(global_cfg.cur_byte, (size_t)5);
    editor_move_cursor(ARROW_UP);
    ASSERT_EQ(global_cfg.cur_byte, (size_t)4);
    free(global_cfg.disassembler_buffer);
}

static void setup_disassembler_mode(uint8_t *code, size_t length, size_t rows) {
    RESET_GLOBAL_CFG();
    global_cfg.file = code;
    global_cfg.num_bytes = length;
    global_cfg.mode = DISASSEMBLER_MODE;
    global_cfg.disassembler_mode = MODE_LONG_COMPAT_64;
    editor_resize(rows + 2, 80);
}

static void test_disassembler_pages_from_centered_selection(void) {
    uint8_t code[100];
    memset(code, 0x90, sizeof(code));
    setup_disassembler_mode(code, sizeof(code), 10);
    global_cfg.cur_byte = 50;

    editor_move_cursor(PAGE_DOWN);
    ASSERT_EQ(global_cfg.cur_byte, (size_t)60);
    ASSERT_EQ(global_cfg.disassembler_buffer[5].start_byte, (size_t)60);
    editor_move_cursor(PAGE_UP);
    ASSERT_EQ(global_cfg.cur_byte, (size_t)50);
    ASSERT_EQ(global_cfg.disassembler_buffer[5].start_byte, (size_t)50);

    editor_resize(7, 80);
    editor_move_cursor(PAGE_DOWN);
    ASSERT_EQ(global_cfg.cur_byte, (size_t)55);
    ASSERT_EQ(global_cfg.disassembler_buffer[2].start_byte, (size_t)55);
    editor_move_cursor(PAGE_UP);
    ASSERT_EQ(global_cfg.cur_byte, (size_t)50);
    free_disassembler_buffer();
}

static void test_disassembler_pages_count_variable_length_instructions(void) {
    /* mov $0x12345678, %eax; add $1, %eax; xor %ebx, %ebx; nop */
    const uint8_t sequence[] = {
        0xB8, 0x78, 0x56, 0x34, 0x12, 0x83, 0xC0, 0x01, 0x31, 0xDB, 0x90
    };
    uint8_t code[sizeof(sequence) * 8];
    for (size_t i = 0; i < 8; ++i)
        memcpy(code + i * sizeof(sequence), sequence, sizeof(sequence));
    setup_disassembler_mode(code, sizeof(code), 5);

    /* Instruction 9, byte 1 -> instruction 14, byte 1, and back. */
    global_cfg.cur_byte = 28;
    editor_move_cursor(PAGE_DOWN);
    ASSERT_EQ(global_cfg.cur_byte, (size_t)42);
    ASSERT_EQ(global_cfg.disassembler_buffer[2].start_byte, (size_t)41);
    editor_move_cursor(PAGE_UP);
    ASSERT_EQ(global_cfg.cur_byte, (size_t)28);

    /* Clamp an offset inside a long instruction to the shorter destination. */
    global_cfg.cur_byte = 26;
    editor_move_cursor(PAGE_DOWN);
    ASSERT_EQ(global_cfg.cur_byte, (size_t)40);
    ASSERT_EQ(global_cfg.disassembler_buffer[2].start_byte, (size_t)38);
    free_disassembler_buffer();
}

static void test_disassembler_pages_clamp_at_file_boundaries(void) {
    uint8_t code[25];
    memset(code, 0x90, sizeof(code));
    setup_disassembler_mode(code, sizeof(code), 10);

    editor_move_cursor(PAGE_UP);
    ASSERT_EQ(global_cfg.cur_byte, (size_t)0);
    global_cfg.cur_byte = 4;
    editor_move_cursor(PAGE_UP);
    ASSERT_EQ(global_cfg.cur_byte, (size_t)0);
    global_cfg.cur_byte = 20;
    editor_move_cursor(PAGE_DOWN);
    ASSERT_EQ(global_cfg.cur_byte, sizeof(code));
    editor_move_cursor(PAGE_DOWN);
    ASSERT_EQ(global_cfg.cur_byte, sizeof(code));
    editor_move_cursor(PAGE_UP);
    ASSERT_EQ(global_cfg.cur_byte, (size_t)15);
    editor_move_cursor(PAGE_UP);
    ASSERT_EQ(global_cfg.cur_byte, (size_t)5);
    editor_move_cursor(PAGE_UP);
    ASSERT_EQ(global_cfg.cur_byte, (size_t)0);
    free_disassembler_buffer();
}

static void test_disassembler_pages_with_invalid_bytes_and_empty_file(void) {
    uint8_t code[] = {0x06, 0x90, 0xE8};
    setup_disassembler_mode(code, sizeof(code), 3);
    editor_move_cursor(PAGE_DOWN);
    ASSERT_EQ(global_cfg.cur_byte, sizeof(code));
    editor_move_cursor(PAGE_UP);
    ASSERT_EQ(global_cfg.cur_byte, (size_t)0);
    free_disassembler_buffer();

    setup_disassembler_mode(NULL, 0, 3);
    editor_move_cursor(PAGE_DOWN);
    editor_move_cursor(PAGE_UP);
    ASSERT_EQ(global_cfg.cur_byte, (size_t)0);
    free_disassembler_buffer();
}

static void test_disassembler_pages_pause_while_too_small(void) {
    uint8_t code[50];
    memset(code, 0x90, sizeof(code));
    setup_disassembler_mode(code, sizeof(code), 5);
    global_cfg.cur_byte = 20;
    editor_resize(2, 10);
    editor_move_cursor(PAGE_DOWN);
    editor_move_cursor(PAGE_UP);
    ASSERT_EQ(global_cfg.cur_byte, (size_t)20);
    editor_resize(5, 24);
    editor_move_cursor(PAGE_DOWN);
    ASSERT_EQ(global_cfg.cur_byte, (size_t)23);
    free_disassembler_buffer();
}

static void test_text_and_hex_pages_preserve_existing_distances(void) {
    setup_text_mode(1000, 80);
    editor_resize(7, 80);
    editor_move_cursor(PAGE_DOWN);
    ASSERT_EQ(global_cfg.cur_byte, (size_t)400);
    editor_move_cursor(PAGE_UP);
    ASSERT_EQ(global_cfg.cur_byte, (size_t)0);
    global_cfg.mode = HEX_MODE;
    switch_mode();
    editor_move_cursor(PAGE_DOWN);
    ASSERT_EQ(global_cfg.cur_byte, (size_t)80);
    editor_move_cursor(PAGE_UP);
    ASSERT_EQ(global_cfg.cur_byte, (size_t)0);
    free_disassembler_buffer();
    teardown();
}

int main(void) {
    printf("test_input:\n");
    RUN_TEST(test_move_right);
    RUN_TEST(test_move_left_at_origin);
    RUN_TEST(test_move_down);
    RUN_TEST(test_move_up_from_row1);
    RUN_TEST(test_vim_keys);
    RUN_TEST(test_move_right_wraps_row);
    RUN_TEST(test_hex_mode_cursor);
    RUN_TEST(test_ctrl_key_macro);
    RUN_TEST(test_partial_row_navigation_after_resize);
    RUN_TEST(test_navigation_pauses_while_too_small);
    RUN_TEST(test_disassembler_navigation_after_resize_and_eof);
    RUN_TEST(test_disassembler_pages_from_centered_selection);
    RUN_TEST(test_disassembler_pages_count_variable_length_instructions);
    RUN_TEST(test_disassembler_pages_clamp_at_file_boundaries);
    RUN_TEST(test_disassembler_pages_with_invalid_bytes_and_empty_file);
    RUN_TEST(test_disassembler_pages_pause_while_too_small);
    RUN_TEST(test_text_and_hex_pages_preserve_existing_distances);
    TEST_REPORT();
}
