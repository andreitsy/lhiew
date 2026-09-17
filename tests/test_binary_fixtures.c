#include "lhiew/types.h"
#include "test_harness.h"
#include "lhiew/disassembler.h"
#include "lhiew/editor.h"
#include "lhiew/file_buffer.h"

#include <sys/mman.h>

#ifndef LHIEW_TEST_FIXTURE_DIR
#error "LHIEW_TEST_FIXTURE_DIR must name the committed fixture directory"
#endif

static void cleanup_fixture(void) {
    if (global_cfg.file && global_cfg.file != MAP_FAILED)
        munmap(global_cfg.file, global_cfg.num_bytes);
    if (global_cfg.fp)
        fclose(global_cfg.fp);
    free(global_cfg.filename);
    free_disassembler_buffer();
    RESET_GLOBAL_CFG();
}

static void open_fixture(const char *name, disassemblerMode mode, size_t rows) {
    cleanup_fixture();
    global_cfg.mode = DISASSEMBLER_MODE;
    global_cfg.disassembler_mode = mode;
    editor_resize(rows + 2, 80);
    char path[1024];
    snprintf(path, sizeof(path), "%s/%s", LHIEW_TEST_FIXTURE_DIR, name);
    open_file_to_view(path);
    switch_mode();
}

static void assert_row(size_t index, size_t start, size_t end,
                       const char *mnemonic, const char *operand1, const char *operand2) {
    const disassemblerRow *row = &global_cfg.disassembler_buffer[index];
    ASSERT_EQ(row->start_byte, start);
    ASSERT_EQ(row->end_byte, end);
    ASSERT(strncmp(row->diss_str, mnemonic, strlen(mnemonic)) == 0);
    if (operand1)
        ASSERT(strstr(row->diss_str, operand1) != NULL);
    if (operand2)
        ASSERT(strstr(row->diss_str, operand2) != NULL);
}

static void assert_unused_rows(size_t first) {
    for (size_t i = first; i < global_cfg.screenrows; ++i) {
        ASSERT_EQ(global_cfg.disassembler_buffer[i].start_byte, (size_t)0);
        ASSERT_EQ(global_cfg.disassembler_buffer[i].end_byte, (size_t)0);
        ASSERT_STR_EQ(global_cfg.disassembler_buffer[i].diss_str, "");
    }
}

static void test_open_and_decode_16bit_fixture(void) {
    const disassemblerMode modes[] = {REAL, MODE_LONG_COMPAT_16};
    for (size_t i = 0; i < sizeof(modes) / sizeof(modes[0]); ++i) {
        open_fixture("raw_x86_16.bin", modes[i], 10);
        ASSERT_NE(global_cfg.fp, NULL);
        ASSERT_EQ(global_cfg.num_bytes, (size_t)8);
        ASSERT_EQ(disassemble_block(0), EXIT_SUCCESS);
        assert_row(0, 0, 3, "mov", "$0x1234", "%ax");
        assert_row(1, 3, 4, "inc", "%ax", NULL);
        assert_row(2, 4, 7, "add", "$0x01", "%ax");
        assert_row(3, 7, 8, "ret", NULL, NULL);
        assert_unused_rows(4);
    }
}

static void test_open_and_decode_32bit_fixture(void) {
    open_fixture("raw_x86_32.bin", MODE_LONG_COMPAT_32, 10);
    ASSERT_EQ(global_cfg.num_bytes, (size_t)11);
    ASSERT_EQ(disassemble_block(0), EXIT_SUCCESS);
    assert_row(0, 0, 5, "mov", "$0x12345678", "%eax");
    assert_row(1, 5, 8, "add", "$0x01", "%eax");
    assert_row(2, 8, 10, "xor", "%ebx, %ebx", NULL);
    assert_row(3, 10, 11, "ret", NULL, NULL);
    assert_unused_rows(4);
}

static void test_open_and_decode_64bit_fixture(void) {
    open_fixture("raw_x86_64.bin", MODE_LONG_COMPAT_64, 10);
    ASSERT_EQ(global_cfg.num_bytes, (size_t)12);
    ASSERT_EQ(disassemble_block(0), EXIT_SUCCESS);
    assert_row(0, 0, 5, "mov", "$0x3C", "%eax");
    assert_row(1, 5, 10, "mov", "$0x2A", "%edi");
    assert_row(2, 10, 12, "syscall", NULL, NULL);
    assert_unused_rows(3);
}

static void test_open_and_decode_elf_entry(void) {
    open_fixture("minimal_exit_x86_64.elf", MODE_LONG_COMPAT_64, 10);
    ASSERT_EQ(global_cfg.num_bytes, (size_t)132);
    ASSERT(memcmp(global_cfg.file, "\x7f" "ELF\x02\x01\x01", 7) == 0);
    /* ELF64 little-endian header: x86-64, executable, entry 0x400078. */
    ASSERT_EQ(global_cfg.file[16], 2);
    ASSERT_EQ(global_cfg.file[18], 62);
    const uint8_t entry[] = {0x78, 0x00, 0x40, 0x00, 0x00, 0x00, 0x00, 0x00};
    ASSERT(memcmp(global_cfg.file + 24, entry, sizeof(entry)) == 0);
    ASSERT_EQ(disassemble_block(0x78), EXIT_SUCCESS);
    assert_row(0, 0x78, 0x7D, "mov", "$0x3C", "%eax");
    assert_row(1, 0x7D, 0x82, "mov", "$0x2A", "%edi");
    assert_row(2, 0x82, 0x84, "syscall", NULL, NULL);
    assert_unused_rows(3);
}

static void test_fixture_resize_inside_instruction_and_eof(void) {
    open_fixture("raw_x86_32.bin", MODE_LONG_COMPAT_32, 3);
    ASSERT_EQ(disassemble_block(0), EXIT_SUCCESS);
    assert_row(2, 8, 10, "xor", "%ebx, %ebx", NULL);
    global_cfg.cur_byte = 6;
    editor_resize(24, 40);
    ASSERT_EQ(global_cfg.cur_byte, (size_t)6);
    ASSERT_EQ(disassemble_block(global_cfg.cur_byte), EXIT_SUCCESS);
    assert_row(0, 5, 8, "add", "$0x01", "%eax");
    assert_row(1, 8, 10, "xor", "%ebx, %ebx", NULL);
    assert_row(2, 10, 11, "ret", NULL, NULL);
    assert_unused_rows(3);

    global_cfg.cur_byte = 10;
    editor_resize(5, 24);
    ASSERT_EQ(disassemble_block(global_cfg.cur_byte), EXIT_SUCCESS);
    assert_row(0, 10, 11, "ret", NULL, NULL);
    assert_unused_rows(1);
    ASSERT_EQ(disassemble_block(global_cfg.num_bytes), EXIT_SUCCESS);
    assert_unused_rows(0);
}

static void test_open_invalid_and_truncated_fixture(void) {
    open_fixture("invalid_truncated.bin", MODE_LONG_COMPAT_64, 10);
    ASSERT_EQ(global_cfg.num_bytes, (size_t)3);
    ASSERT_EQ(disassemble_block(0), EXIT_SUCCESS);
    assert_row(0, 0, 1, "db 06", NULL, NULL);
    assert_row(1, 1, 2, "nop", NULL, NULL);
    assert_row(2, 2, 3, "db E8", NULL, NULL);
    assert_unused_rows(3);
    ASSERT_EQ(disassemble_block(2), EXIT_SUCCESS);
    assert_row(0, 2, 3, "db E8", NULL, NULL);
    assert_unused_rows(1);
}

static void test_open_empty_fixture(void) {
    open_fixture("empty.bin", MODE_LONG_COMPAT_64, 3);
    ASSERT_NE(global_cfg.fp, NULL);
    ASSERT_EQ(global_cfg.num_bytes, (size_t)0);
    ASSERT_EQ(global_cfg.file, NULL);
    ASSERT_EQ(disassemble_block(0), EXIT_SUCCESS);
    assert_unused_rows(0);
}

int main(void) {
    printf("test_binary_fixtures:\n");
    RUN_TEST(test_open_and_decode_16bit_fixture);
    RUN_TEST(test_open_and_decode_32bit_fixture);
    RUN_TEST(test_open_and_decode_64bit_fixture);
    RUN_TEST(test_open_and_decode_elf_entry);
    RUN_TEST(test_fixture_resize_inside_instruction_and_eof);
    RUN_TEST(test_open_invalid_and_truncated_fixture);
    RUN_TEST(test_open_empty_fixture);
    cleanup_fixture();
    TEST_REPORT();
}
