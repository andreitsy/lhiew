#include "lhiew/types.h"
#include "test_harness.h"
#include "lhiew/append_buffer.h"
#include "lhiew/editor.h"
#include "lhiew/hex_edit.h"
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
    if (global_cfg.file && global_cfg.file != MAP_FAILED)
        munmap(global_cfg.file, global_cfg.num_bytes);
    global_cfg.file = NULL;
    free(global_cfg.disassembler_buffer);
    global_cfg.disassembler_buffer = NULL;
    if (global_cfg.fp) { fclose(global_cfg.fp); global_cfg.fp = NULL; }
    free(global_cfg.filename);
    global_cfg.filename = NULL;
    unlink(TMP_FILE);
}

static char *plain_output(const append_buffer *ab) {
    char *plain = malloc(ab->len + 1);
    size_t out = 0;
    for (size_t i = 0; i < ab->len; ++i) {
        if (ab->buffer[i] == '\x1b' && i + 1 < ab->len && ab->buffer[i + 1] == '[') {
            i += 2;
            while (i < ab->len && !(ab->buffer[i] >= '@' && ab->buffer[i] <= '~'))
                ++i;
        } else {
            plain[out++] = ab->buffer[i];
        }
    }
    plain[out] = '\0';
    return plain;
}

/* Check actual terminal cells, including explicit positioning in tiny windows. */
static int frame_fits(const append_buffer *ab, size_t rows, size_t cols) {
    size_t row = 0, col = 0;
    for (size_t i = 0; i < ab->len; ++i) {
        unsigned char byte = (unsigned char)ab->buffer[i];
        if (byte == '\x1b') {
            if (++i >= ab->len || ab->buffer[i] != '[')
                return 0;
            size_t start = ++i;
            while (i < ab->len && !(ab->buffer[i] >= '@' && ab->buffer[i] <= '~'))
                ++i;
            if (i == ab->len)
                return 0;
            /* EL at a pending wrap erases the last cell just written. */
            if (ab->buffer[i] == 'K' && col == cols)
                return 0;
            if (ab->buffer[i] == 'H') {
                char parameters[64] = {0};
                size_t length = i - start;
                if (length >= sizeof(parameters))
                    return 0;
                memcpy(parameters, ab->buffer + start, length);
                size_t terminal_row = 1, terminal_col = 1;
                if (length && sscanf(parameters, "%zu;%zu", &terminal_row, &terminal_col) != 2)
                    return 0;
                if (!terminal_row || !terminal_col || terminal_row > rows || terminal_col > cols)
                    return 0;
                row = terminal_row - 1;
                col = terminal_col - 1;
            }
        } else if (byte == '\r') {
            col = 0;
        } else if (byte == '\n') {
            if (++row >= rows)
                return 0;
        } else {
            if (byte < 32 || byte >= 127 || row >= rows || col >= cols)
                return 0;
            ++col;
        }
    }
    return 1;
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

static void test_text_rows_use_displayed_row_length(void) {
    const char data[] = "ABCDEFGHIJKLM";
    setup_file_data(data, sizeof(data) - 1);
    global_cfg.cur_screencols = 5;
    global_cfg.cy = 2;
    append_buffer ab = ABUF_INIT;
    draw_row_text(0, &ab);
    ASSERT_EQ(ab.len, (size_t)5);
    ASSERT(memcmp(ab.buffer, "ABCDE", 5) == 0);
    free_append_buffer(&ab);

    global_cfg.cy = 0;
    global_cfg.rowoff = 1;
    draw_row_text(1, &ab);
    ASSERT_EQ(ab.len, (size_t)3);
    ASSERT(memcmp(ab.buffer, "KLM", 3) == 0);
    free_append_buffer(&ab);
    draw_row_text(2, &ab);
    ASSERT_EQ(ab.len, (size_t)0);
    free_append_buffer(&ab);
    teardown();
}

static void test_hex_partial_and_missing_rows(void) {
    const char data[] = "ABCDEFGHIJKLMNOPQR";
    setup_file_data(data, sizeof(data) - 1);
    global_cfg.mode = HEX_MODE;
    global_cfg.cur_byte = 17;
    editor_resize(5, 80);
    append_buffer ab = ABUF_INIT;
    draw_row_hex(0, &ab);
    char *plain = plain_output(&ab);
    ASSERT_EQ(strlen(plain), (size_t)77);
    ASSERT(strstr(plain, "00000000  41 42 43 44") != NULL);
    ASSERT(strstr(plain, "|ABCDEFGHIJKLMNOP|") != NULL);
    free(plain);
    free_append_buffer(&ab);

    global_cfg.rowoff = 1;
    draw_row_hex(0, &ab);
    plain = plain_output(&ab);
    ASSERT_EQ(strlen(plain), (size_t)77);
    ASSERT(strstr(plain, "00000010  51 52 ") != NULL);
    ASSERT(strstr(plain, "|QR              |") != NULL);
    ASSERT_EQ(memchr(ab.buffer, '\0', ab.len), NULL);
    ASSERT_GT(ab.len, strlen(plain));
    free(plain);
    free_append_buffer(&ab);
    draw_row_hex(1, &ab);
    ASSERT_EQ(ab.len, (size_t)1);
    ASSERT_EQ(ab.buffer[0], '~');
    free_append_buffer(&ab);
    teardown();
}

static void test_disassembler_compact_and_wide_rows(void) {
    const uint8_t data[] = {0x90};
    setup_file_data(data, sizeof(data));
    global_cfg.mode = DISASSEMBLER_MODE;
    const size_t widths[] = {24, 40, 80, 120};
    for (size_t i = 0; i < sizeof(widths) / sizeof(widths[0]); ++i) {
        editor_resize(5, widths[i]);
        global_cfg.disassembler_buffer[0].start_byte = 0;
        global_cfg.disassembler_buffer[0].end_byte = 1;
        strcpy(global_cfg.disassembler_buffer[0].diss_str,
               "mov %very_long_source_operand, %very_long_destination_operand");
        append_buffer ab = ABUF_INIT;
        draw_row_disassembler(0, &ab);
        char *plain = plain_output(&ab);
        ASSERT(strlen(plain) <= widths[i]);
        ASSERT(strstr(plain, "00000000  ") != NULL);
        ASSERT(strstr(plain, "mov ") != NULL);
        if (widths[i] < 80) {
            ASSERT(strstr(plain, "90") == NULL);
            ASSERT_EQ(plain[strlen(plain) - 1], '~');
        } else {
            ASSERT(strstr(plain, "90") != NULL);
        }
        ASSERT_GT(ab.len, strlen(plain));
        ASSERT_EQ(memchr(ab.buffer, '\0', ab.len), NULL);
        free(plain);
        free_append_buffer(&ab);
    }
    teardown();
}

static void test_frames_fit_supported_sizes_in_all_modes(void) {
    uint8_t data[257];
    memset(data, 0x90, sizeof(data));
    setup_file_data(data, sizeof(data));
    const size_t widths[] = {24, 25, 31, 40, 63, 65, 66, 76, 77, 79, 80, 81, 120};
    const size_t heights[] = {5, 6, 8, 24};
    for (int mode = TEXT_MODE; mode <= DISASSEMBLER_MODE; ++mode) {
        global_cfg.mode = (editorMode)mode;
        for (size_t w = 0; w < sizeof(widths) / sizeof(widths[0]); ++w) {
            for (size_t h = 0; h < sizeof(heights) / sizeof(heights[0]); ++h) {
                global_cfg.cur_byte = sizeof(data) - 1;
                editor_resize(heights[h], widths[w]);
                append_buffer ab = ABUF_INIT;
                editor_draw_screen(&ab);
                ASSERT(frame_fits(&ab, heights[h], widths[w]));
                char *plain = plain_output(&ab);
                ASSERT(strstr(plain, "256:257") != NULL);
                ASSERT(strstr(plain, "quit") != NULL);
                if (mode == DISASSEMBLER_MODE)
                    ASSERT(strstr(plain, "nop") != NULL);
                free(plain);
                free_append_buffer(&ab);
            }
        }
    }
    teardown();
}

static void test_tiny_window_message_and_recovery(void) {
    const char data[] = "ABC";
    setup_file_data(data, sizeof(data) - 1);
    const size_t sizes[][2] = {{1, 1}, {1, 24}, {2, 40}, {4, 80}, {8, 12}, {5, 23}};
    for (size_t i = 0; i < sizeof(sizes) / sizeof(sizes[0]); ++i) {
        editor_resize(sizes[i][0], sizes[i][1]);
        append_buffer ab = ABUF_INIT;
        editor_draw_screen(&ab);
        ASSERT(frame_fits(&ab, sizes[i][0], sizes[i][1]));
        char *plain = plain_output(&ab);
        if (sizes[i][1] >= 12)
            ASSERT(strstr(plain, "24x5") != NULL);
        if (sizes[i][1] >= 24)
            ASSERT(strstr(plain, "quit") != NULL);
        free(plain);
        free_append_buffer(&ab);
    }

    editor_resize(5, 24);
    append_buffer ab = ABUF_INIT;
    editor_draw_screen(&ab);
    ASSERT(frame_fits(&ab, 5, 24));
    char *plain = plain_output(&ab);
    ASSERT(strstr(plain, "ABC") != NULL);
    ASSERT(strstr(plain, "TEXT") != NULL);
    ASSERT(strstr(plain, "small") == NULL);
    free(plain);
    free_append_buffer(&ab);
    teardown();
}

static void test_empty_and_eof_frames_fit(void) {
    const char data[] = "ABCDEFGHIJKLMNOPQRSTUVWX";
    setup_file_data(data, sizeof(data) - 1);
    global_cfg.cur_byte = global_cfg.num_bytes;
    for (int mode = TEXT_MODE; mode <= DISASSEMBLER_MODE; ++mode) {
        global_cfg.mode = (editorMode)mode;
        editor_resize(5, 24);
        append_buffer ab = ABUF_INIT;
        editor_draw_screen(&ab);
        ASSERT(frame_fits(&ab, 5, 24));
        free_append_buffer(&ab);
    }
    teardown();
    RESET_GLOBAL_CFG();
    for (int mode = TEXT_MODE; mode <= DISASSEMBLER_MODE; ++mode) {
        global_cfg.mode = (editorMode)mode;
        editor_resize(5, 24);
        append_buffer ab = ABUF_INIT;
        editor_draw_screen(&ab);
        ASSERT(frame_fits(&ab, 5, 24));
        free_append_buffer(&ab);
    }
    free(global_cfg.disassembler_buffer);
    global_cfg.disassembler_buffer = NULL;
}

static void test_status_and_messages_sanitize_control_bytes(void) {
    const char data[] = "ABC";
    setup_file_data(data, sizeof(data) - 1);
    free(global_cfg.filename);
    global_cfg.filename = strdup("/tmp/bad\x1b[2J\nname");
    editor_resize(5, 40);
    editor_set_status_message("bad\x1b[2J\tmessage");
    append_buffer ab = ABUF_INIT;
    editor_draw_screen(&ab);
    ASSERT(frame_fits(&ab, 5, 40));
    char *plain = plain_output(&ab);
    ASSERT(strstr(plain, "bad@[2J.name") != NULL);
    ASSERT(strstr(plain, "bad@[2J.message") != NULL);
    free(plain);
    free_append_buffer(&ab);
    teardown();
}

static void test_edit_cursor_and_prompts_fit_after_resize(void) {
    const char data[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ";
    setup_file_data(data, sizeof(data) - 1);
    global_cfg.mode = HEX_MODE;
    ASSERT(hex_edit_begin());
    const size_t widths[] = {24, 31, 40, 80, 120};
    for (size_t i = 0; i < sizeof(widths) / sizeof(widths[0]); ++i) {
        global_cfg.cur_byte = sizeof(data) - 2;
        editor_resize(5, widths[i]);
        for (int pane = 0; pane < 3; ++pane) {
            global_cfg.edit_ascii = pane == 2;
            global_cfg.edit_nibble = pane == 1;
            append_buffer frame = ABUF_INIT;
            editor_draw_screen(&frame);
            ASSERT(frame_fits(&frame, 5, widths[i]));
            free_append_buffer(&frame);
        }
        /* Prompts must fit even the minimum supported screen and a full
           machine-width offset must remain visible while being entered. */
        global_cfg.goto_prompt = 1;
        snprintf(global_cfg.goto_input, sizeof(global_cfg.goto_input), "0x%zx", SIZE_MAX);
        global_cfg.goto_length = strlen(global_cfg.goto_input);
        append_buffer ab = ABUF_INIT;
        editor_draw_screen(&ab);
        ASSERT(frame_fits(&ab, 5, widths[i]));
        char *plain = plain_output(&ab);
        ASSERT(strstr(plain, global_cfg.goto_input) != NULL);
        free(plain);
        free_append_buffer(&ab);
        global_cfg.goto_prompt = 0;
        global_cfg.edit_exit_prompt = 1;
        editor_draw_screen(&ab);
        ASSERT(frame_fits(&ab, 5, widths[i]));
        plain = plain_output(&ab);
        ASSERT(strstr(plain, "Unsaved changes") != NULL);
        ASSERT(strstr(plain, "s save d discard") != NULL ||
               strstr(plain, "s save | d discard") != NULL);
        free(plain);
        free_append_buffer(&ab);
        global_cfg.edit_exit_prompt = 0;
    }
    hex_edit_cancel();
    teardown();
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
    RUN_TEST(test_text_rows_use_displayed_row_length);
    RUN_TEST(test_hex_partial_and_missing_rows);
    RUN_TEST(test_disassembler_compact_and_wide_rows);
    RUN_TEST(test_frames_fit_supported_sizes_in_all_modes);
    RUN_TEST(test_tiny_window_message_and_recovery);
    RUN_TEST(test_empty_and_eof_frames_fit);
    RUN_TEST(test_status_and_messages_sanitize_control_bytes);
    RUN_TEST(test_edit_cursor_and_prompts_fit_after_resize);
    TEST_REPORT();
}
