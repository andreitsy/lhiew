#include "lhiew/types.h"
#include "test_harness.h"
#include "lhiew/disassembler.h"
#include "lhiew/editor.h"
#include "lhiew/architecture.h"

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

    ASSERT_EQ(global_cfg.disassembler_buffer[0].start_byte, (size_t)0);
    ASSERT_EQ(global_cfg.disassembler_buffer[5].start_byte, (size_t)5);
    ASSERT(strstr(global_cfg.disassembler_buffer[5].diss_str, "ret") != NULL);

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
    size_t selected = global_cfg.screenrows / 2;
    ASSERT_EQ(global_cfg.disassembler_buffer[selected].start_byte, sizeof(code) - 1);
    ASSERT_EQ(global_cfg.disassembler_buffer[selected].end_byte, sizeof(code));
    for (size_t i = selected + 1; i < global_cfg.screenrows; ++i) {
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

static void test_disassemble_centers_selection_across_redraws_and_resize(void) {
    /* Repeated 10-byte movabs instructions require more than 128 bytes of
       preceding context in tall windows. Immediate bytes are also valid NOPs. */
    uint8_t code[1000];
    memset(code, 0x90, sizeof(code));
    for (size_t i = 0; i < sizeof(code); i += 10) {
        code[i] = 0x48;
        code[i + 1] = 0xB8;
    }
    setup_with_bytes(code, sizeof(code));
    global_cfg.mode = DISASSEMBLER_MODE;
    global_cfg.cur_byte = 603;
    const size_t heights[] = {12, 62, 5, 24};
    for (size_t h = 0; h < sizeof(heights) / sizeof(heights[0]); ++h) {
        editor_resize(heights[h], 80);
        for (size_t redraw = 0; redraw < 3; ++redraw) {
            ASSERT_EQ(disassemble_block(global_cfg.cur_byte), EXIT_SUCCESS);
            ASSERT_EQ(global_cfg.cur_byte, (size_t)603);
            size_t middle = global_cfg.screenrows / 2;
            for (size_t row = 0; row < global_cfg.screenrows; ++row) {
                const disassemblerRow *instruction = &global_cfg.disassembler_buffer[row];
                ASSERT_EQ(instruction->start_byte, 600 - middle * 10 + row * 10);
                ASSERT_EQ(instruction->end_byte, instruction->start_byte + 10);
                ASSERT(strstr(instruction->diss_str, "mov") != NULL);
            }
        }
    }
    teardown();
}

static void test_disassemble_centers_near_start_and_with_invalid_bytes(void) {
    uint8_t code[64];
    memset(code, 0x06, sizeof(code)); /* Invalid in 64-bit mode. */
    setup_with_bytes(code, sizeof(code));
    ASSERT_EQ(disassemble_block(2), EXIT_SUCCESS);
    ASSERT_EQ(global_cfg.disassembler_buffer[0].start_byte, (size_t)0);
    ASSERT_EQ(global_cfg.disassembler_buffer[2].start_byte, (size_t)2);

    ASSERT_EQ(disassemble_block(30), EXIT_SUCCESS);
    for (size_t row = 0; row < global_cfg.screenrows; ++row) {
        ASSERT_EQ(global_cfg.disassembler_buffer[row].start_byte, 25 + row);
        ASSERT_EQ(global_cfg.disassembler_buffer[row].end_byte, 26 + row);
        ASSERT_STR_EQ(global_cfg.disassembler_buffer[row].diss_str, "db 06");
    }
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

static void test_disassemble_respects_nested_section_mapping(void) {
    /* ELF32 segment maps the whole image at 0x10000. Its six-byte code section
       maps offset 0x100 at 0x5000 instead. The E8 just before that section must
       not consume its bytes, even when rendering forward from the segment. */
    uint8_t file[272] = {
        [0] = 0x7f, [1] = 'E', [2] = 'L', [3] = 'F',
        [4] = 1, [5] = 1, [6] = 1,
        [16] = 2, [18] = 3, [20] = 1,
        [24] = 250, [26] = 1, [28] = 52, [32] = 96,
        [40] = 52, [42] = 32, [44] = 1, [46] = 40, [48] = 2,
        [52] = 1, [62] = 1, [68] = 16, [69] = 1,
        [72] = 16, [73] = 1, [76] = 5,
        [140] = 1, [144] = 6, [149] = 0x50, [153] = 1, [156] = 6,
        [250] = 0x90, [251] = 0x90, [252] = 0x90,
        [253] = 0x90, [254] = 0x90, [255] = 0xe8,
        [256] = 0xe8, [261] = 0xc3,
    };
    for (int manual = 0; manual < 2; ++manual) {
        if (manual)
            file[18] = 0xff; /* Unknown CPU, but the mapping is still valid. */
        setup_with_bytes(file, sizeof(file));
        architecture_detect_file();
        if (manual) {
            ASSERT_EQ(global_cfg.architecture, ARCH_UNKNOWN);
            for (size_t i = 1; i < architecture_profile_count(); ++i) {
                const architectureSpec *spec = &architecture_profile(i)->spec;
                if (spec->id == ARCH_X86 && spec->x86_mode == MODE_LONG_COMPAT_32) {
                    architecture_select(i);
                    break;
                }
            }
        }
        ASSERT_EQ(disassemble_block(254), EXIT_SUCCESS);
        ASSERT_EQ(global_cfg.disassembler_buffer[6].start_byte, (size_t)255);
        ASSERT_STR_EQ(global_cfg.disassembler_buffer[6].diss_str, "db E8");
        ASSERT_EQ(global_cfg.disassembler_buffer[7].start_byte, (size_t)256);
        ASSERT_EQ(global_cfg.disassembler_buffer[7].end_byte, (size_t)261);
        ASSERT(strstr(global_cfg.disassembler_buffer[7].diss_str, "5005") != NULL);
        ASSERT_STR_EQ(global_cfg.disassembler_buffer[8].diss_str, "ret");
        teardown();
    }
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
    RUN_TEST(test_disassemble_centers_selection_across_redraws_and_resize);
    RUN_TEST(test_disassemble_centers_near_start_and_with_invalid_bytes);
    RUN_TEST(test_disassemble_invalid_and_truncated_bytes);
    RUN_TEST(test_disassemble_tall_window);
    RUN_TEST(test_disassemble_empty_or_missing_file);
    RUN_TEST(test_disassemble_without_visible_rows_or_buffer);
    RUN_TEST(test_free_disassembler_buffer_null_safe);
    RUN_TEST(test_disassemble_respects_nested_section_mapping);
    TEST_REPORT();
}
