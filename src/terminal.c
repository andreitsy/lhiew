#include "lhiew/types.h"
#include "lhiew/terminal.h"
#include "lhiew/editor.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

_Noreturn void die_safely(const char *s) {
    write(STDOUT_FILENO, "\x1b[2J", 4);
    write(STDOUT_FILENO, "\x1b[H", 3);
    perror(s);
    exit(EXIT_FAILURE);
}

void disable_raw_mode(void) {
    if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &global_cfg.orig_termios) == -1)
        die_safely("tcsetattr");
    /* Compact/too-small views also hide the cursor. Always restore it. */
    write(STDOUT_FILENO, "\x1b[m\x1b[?25h", 9);
    if (global_cfg.fp != NULL)
        fclose(global_cfg.fp);
}

void enable_raw_mode(void) {
    if (tcgetattr(STDIN_FILENO, &global_cfg.orig_termios) == -1)
        die_safely("tcgetattr");
    atexit(disable_raw_mode);
    struct termios raw = global_cfg.orig_termios;
    raw.c_iflag &= ~(tcflag_t)(BRKINT | ICRNL | INPCK | ISTRIP | IXON);
    raw.c_oflag &= ~(tcflag_t)OPOST;
    raw.c_cflag |= CS8;
    raw.c_lflag &= ~(tcflag_t)(ECHO | ICANON | IEXTEN | ISIG);
    raw.c_cc[VMIN] = 0;
    raw.c_cc[VTIME] = 1;
    if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) == -1)
        die_safely("tcsetattr");
}

int editor_read_key(void) {
    ssize_t nread;
    char c;
    while ((nread = read(STDIN_FILENO, &c, 1)) != 1) {
        if (nread == -1 && errno != EAGAIN && errno != EINTR)
            die_safely("read");
        /* VTIME wakes us every 100 ms, including while no keys are pressed. */
        if (editor_update_window_size())
            return 0;
    }
    if (c == '\x1b') {
        char introducer;
        if (read(STDIN_FILENO, &introducer, 1) != 1) return '\x1b';
        if (introducer != '[' && introducer != 'O')
            return 0;
        char seq[32];
        size_t length = 0;
        int overflow = 0;
        char byte;
        do {
            if (read(STDIN_FILENO, &byte, 1) != 1)
                return 0;
            if (length < sizeof(seq) - 1)
                seq[length++] = byte;
            else
                overflow = 1;
        } while ((unsigned char)byte < 0x40 || (unsigned char)byte > 0x7e);
        seq[length] = '\0';
        /* Consume unknown and oversized sequences in full, without stray keys. */
        if (overflow)
            return 0;
        if (introducer == '[' &&
            (!strcmp(seq, "1;2P") || !strcmp(seq, "23~") || !strcmp(seq, "11;2~")))
            return SHIFT_F1;
        if ((introducer == 'O' && !strcmp(seq, "R")) ||
            (introducer == '[' && !strcmp(seq, "13~"))) return F3_KEY;
        if (introducer == '[') {
            if (!strcmp(seq, "15~")) return F5_KEY;
            if (!strcmp(seq, "18~")) return F7_KEY;
            if (!strcmp(seq, "18;2~") || !strcmp(seq, "31~")) return SHIFT_F7;
            if (!strcmp(seq, "19~")) return F8_KEY;
            if (!strcmp(seq, "20~")) return F9_KEY;
            if (!strcmp(seq, "21~")) return F10_KEY;
            if (!strcmp(seq, "1;5H") || !strcmp(seq, "1;5~") ||
                !strcmp(seq, "7;5~")) return CTRL_HOME;
            if (!strcmp(seq, "1;5F") || !strcmp(seq, "4;5~") ||
                !strcmp(seq, "8;5~")) return CTRL_END;
            if (!strcmp(seq, "1~") || !strcmp(seq, "7~")) return HOME_KEY;
            if (!strcmp(seq, "4~") || !strcmp(seq, "8~")) return END_KEY;
        }
        if (!strcmp(seq, "H")) return HOME_KEY;
        if (!strcmp(seq, "F")) return END_KEY;
        if (!strcmp(seq, "3~")) return DEL_KEY;
        if (!strcmp(seq, "5~")) return PAGE_UP;
        if (!strcmp(seq, "6~")) return PAGE_DOWN;
        switch (byte) {
            case 'A': return ARROW_UP;
            case 'B': return ARROW_DOWN;
            case 'C': return ARROW_RIGHT;
            case 'D': return ARROW_LEFT;
        }
        return 0;
    }
    return c;
}

int get_cursor_position(size_t *rows, size_t *cols) {
    char buf[32] = {0};
    unsigned int i = 0;
    while (i < sizeof(buf) - 1) {
        if (read(STDIN_FILENO, &buf[i], 1) != 1) break;
        if (buf[i] == 'R') break;
        i++;
    }
    buf[i] = '\0';
    if (buf[0] != '\x1b' || buf[1] != '[') return -1;
    if (sscanf(&buf[2], "%zu;%zu", rows, cols) != 2)
        return -1;
    return 0;
}

int get_window_size(size_t *rows, size_t *cols) {
    struct winsize ws;
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == -1 &&
        ioctl(STDIN_FILENO, TIOCGWINSZ, &ws) == -1) {
        if (write(STDOUT_FILENO, "\x1b[999C\x1b[999B\x1b[6n", 16) != 16)
            return -1;
        return get_cursor_position(rows, cols);
    }
    *cols = ws.ws_col;
    *rows = ws.ws_row;
    return 0;
}
