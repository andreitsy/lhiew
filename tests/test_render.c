#include "lhiew/types.h"
#include "test_harness.h"
#include "lhiew/append_buffer.h"
#include "lhiew/render.h"

#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

static const char *TMP_FILE = "/tmp/lhiew_test_render.bin";

static void setup_file_data(const void *data, size_t len) {
    /* Write temp file and mmap it via the global */
    FILE *f = fopen(TMP_FILE, "wb");
    fwrite(data, 1, len, f);
    fclose(f);

    RESET_GLOBAL_CFG();
    global_cfg.fp = fopen(TMP_FILE, "rb");
    global_cfg.filename = strdup(TMP_FILE);
    global_cfg.cur_screencols = 80;
    global_cfg.screencols = 80;

    int fd = fileno(global_cfg.fp);
    struct stat st;
    fstat(fd, &st);
    global_cfg.num_bytes = st.st_size;
    global_cfg.file = mmap(NULL, global_cfg.num_bytes, PROT_READ, MAP_PRIVATE, fd, 0);
    global_cfg.numrows = global_cfg.num_bytes / global_cfg.cur_screencols + 1;
    global_cfg.screenrows = 24;
}

static void teardown(void) {
    if (global_cfg.fp) { fclose(global_cfg.fp); global_cfg.fp = NULL; }
    free(global_cfg.filename);
    global_cfg.filename = NULL;
    unlink(TMP_FILE);
}

static void test_get_byte_position_origin(void) {
    RESET_GLOBAL_CFG();
    global_cfg.cx = 0;
    global_cfg.cy = 0;
    global_cfg.cur_screencols = 80;
    global_cfg.num_bytes = 1000;

    ASSERT_EQ(get_byte_position(), (size_t)0);
}

static void test_get_byte_position_mid(void) {
    RESET_GLOBAL_CFG();
    global_cfg.cx = 10;
    global_cfg.cy = 5;
    global_cfg.cur_screencols = 80;
    global_cfg.num_bytes = 1000;

    /* 10 + 80*5 = 410 */
    ASSERT_EQ(get_byte_position(), (size_t)410);
}

static void test_get_byte_position_clamp(void) {
    RESET_GLOBAL_CFG();
    global_cfg.cx = 50;
    global_cfg.cy = 100;
    global_cfg.cur_screencols = 80;
    global_cfg.num_bytes = 100;

    /* 50 + 80*100 = 8050 > 100 → clamp to 100 */
    ASSERT_EQ(get_byte_position(), (size_t)100);
}

static void test_draw_row_text_printable(void) {
    const char data[] = "ABCDEFGHIJ";
    setup_file_data(data, 10);
    global_cfg.cur_screencols = 10;
    global_cfg.numrows = 1;
    global_cfg.cy = 0;

    append_buffer ab = ABUF_INIT;
    draw_row_text(0, &ab);

    ASSERT_EQ(ab.len, (size_t)10);
    ASSERT(memcmp(ab.buffer, "ABCDEFGHIJ", 10) == 0);

    free_append_buffer(&ab);
    teardown();
}

static void test_draw_row_text_control_chars(void) {
    /* Control chars (0x01-0x1a) should render as '.' */
    const uint8_t data[] = {0x01, 0x02, 0x0A, 0x41};
    setup_file_data(data, 4);
    global_cfg.cur_screencols = 4;
    global_cfg.numrows = 1;
    global_cfg.cy = 0;

    append_buffer ab = ABUF_INIT;
    draw_row_text(0, &ab);

    ASSERT_EQ(ab.len, (size_t)4);
    ASSERT_EQ(ab.buffer[0], '.');
    ASSERT_EQ(ab.buffer[1], '.');
    ASSERT_EQ(ab.buffer[2], '.');
    ASSERT_EQ(ab.buffer[3], 'A');

    free_append_buffer(&ab);
    teardown();
}

static void test_draw_row_text_high_bytes(void) {
    /* Non-printable, non-control bytes should render as '?' */
    const uint8_t data[] = {0x80, 0xFF};
    setup_file_data(data, 2);
    global_cfg.cur_screencols = 2;
    global_cfg.numrows = 1;
    global_cfg.cy = 0;

    append_buffer ab = ABUF_INIT;
    draw_row_text(0, &ab);

    ASSERT_EQ(ab.len, (size_t)2);
    ASSERT_EQ(ab.buffer[0], '?');
    ASSERT_EQ(ab.buffer[1], '?');

    free_append_buffer(&ab);
    teardown();
}

static void test_editor_scroll_down(void) {
    RESET_GLOBAL_CFG();
    global_cfg.screenrows = 24;
    global_cfg.cur_screencols = 80;
    global_cfg.numrows = 100;
    global_cfg.rowoff = 0;
    global_cfg.coloff = 0;
    global_cfg.cy = 30;
    global_cfg.cx = 5;

    editor_scroll();

    /* cy(30) >= rowoff(0) + screenrows(24), so rowoff = 30-24+1 = 7 */
    ASSERT_EQ(global_cfg.rowoff, (size_t)7);
}

static void test_editor_scroll_up(void) {
    RESET_GLOBAL_CFG();
    global_cfg.screenrows = 24;
    global_cfg.cur_screencols = 80;
    global_cfg.numrows = 100;
    global_cfg.rowoff = 20;
    global_cfg.coloff = 0;
    global_cfg.cy = 10;
    global_cfg.cx = 0;

    editor_scroll();

    /* cy(10) < rowoff(20), so rowoff = cy = 10 */
    ASSERT_EQ(global_cfg.rowoff, (size_t)10);
}

static void test_set_status_message(void) {
    RESET_GLOBAL_CFG();
    editor_set_status_message("test %d", 42);
    ASSERT_STR_EQ(global_cfg.statusmsg, "test 42");
    ASSERT_GT(global_cfg.statusmsg_time, (time_t)0);
}

int main(void) {
    printf("test_render:\n");
    RUN_TEST(test_get_byte_position_origin);
    RUN_TEST(test_get_byte_position_mid);
    RUN_TEST(test_get_byte_position_clamp);
    RUN_TEST(test_draw_row_text_printable);
    RUN_TEST(test_draw_row_text_control_chars);
    RUN_TEST(test_draw_row_text_high_bytes);
    RUN_TEST(test_editor_scroll_down);
    RUN_TEST(test_editor_scroll_up);
    RUN_TEST(test_set_status_message);
    TEST_REPORT();
}
