#include "lhiew/types.h"
#include "test_harness.h"
#include "lhiew/disassembler.h"

#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

static const char *TMP_FILE = "/tmp/lhiew_test_dasm.bin";

static void setup_with_bytes(const uint8_t *data, size_t len) {
    RESET_GLOBAL_CFG();

    FILE *f = fopen(TMP_FILE, "wb");
    fwrite(data, 1, len, f);
    fclose(f);

    global_cfg.fp = fopen(TMP_FILE, "rb");
    global_cfg.filename = strdup(TMP_FILE);
    int fd = fileno(global_cfg.fp);
    struct stat st;
    fstat(fd, &st);
    global_cfg.num_bytes = st.st_size;
    global_cfg.file = mmap(NULL, global_cfg.num_bytes, PROT_READ, MAP_PRIVATE, fd, 0);

    global_cfg.cur_screencols = 80;
    global_cfg.screencols = 80;
    global_cfg.screenrows = 10;
    global_cfg.disassembler_mode = MODE_LONG_COMPAT_64;
    global_cfg.disassembler_buffer = calloc(global_cfg.screenrows, sizeof(disassemblerRow));
}

static void teardown(void) {
    free(global_cfg.disassembler_buffer);
    global_cfg.disassembler_buffer = NULL;
    if (global_cfg.fp) { fclose(global_cfg.fp); global_cfg.fp = NULL; }
    free(global_cfg.filename);
    global_cfg.filename = NULL;
    unlink(TMP_FILE);
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

    /* First row should start at or before byte 5 */
    ASSERT(global_cfg.disassembler_buffer[0].start_byte <= 5);

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
    RUN_TEST(test_free_disassembler_buffer_null_safe);
    TEST_REPORT();
}
