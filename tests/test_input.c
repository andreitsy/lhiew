#include "lhiew/types.h"
#include "test_harness.h"
#include "lhiew/input.h"
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
    TEST_REPORT();
}
