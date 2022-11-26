#include "editor.h"

void die_safely(const char *s) {
    write(STDOUT_FILENO, "\x1b[2J", 4);
    write(STDOUT_FILENO, "\x1b[H", 3);
    perror(s);
    exit(EXIT_FAILURE);
}

size_t get_row_len(void) {
    size_t tmp_len = global_cfg.num_bytes - global_cfg.cy*global_cfg.cur_screencols;
    return tmp_len < global_cfg.cur_screencols ? tmp_len : global_cfg.cur_screencols;
}

void switch_mode(void) {
    switch (global_cfg.mode) {
        case TEXT_MODE:
            global_cfg.cur_screencols = global_cfg.screencols;
            break;
        case HEX_MODE:
            global_cfg.cur_screencols = HEX_BYTE_LENGTH;
            break;
        case DISSASEMBLER_MODE:
            global_cfg.cur_screencols = global_cfg.screencols;
            break;
    }
    global_cfg.cy = global_cfg.cur_byte / global_cfg.cur_screencols;
    global_cfg.cx = global_cfg.cur_byte % global_cfg.cur_screencols;
    global_cfg.numrows = global_cfg.num_bytes / global_cfg.cur_screencols + 1;
}

void disable_raw_mode(void) {
    if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &global_cfg.orig_termios) == -1)
        die_safely("tcsetattr");
    if (global_cfg.mode == DISSASEMBLER_MODE) {
        printf("\x1b[?25h"); // make cursor visible
    }
    fclose(global_cfg.fp);
}

void enable_raw_mode(void) {
    if (tcgetattr(STDIN_FILENO, &global_cfg.orig_termios) == -1)
        die_safely("tcgetattr");
    tcgetattr(STDIN_FILENO, &global_cfg.orig_termios);
    atexit(disable_raw_mode);
    struct termios raw = global_cfg.orig_termios;
    raw.c_iflag &= ~(BRKINT | ICRNL | INPCK | ISTRIP | IXON);
    raw.c_oflag &= ~(OPOST);
    raw.c_cflag |= (CS8);
    raw.c_lflag &= ~(ECHO | ICANON | IEXTEN | ISIG);
    raw.c_cc[VMIN] = 0;
    raw.c_cc[VTIME] = 1;
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw);
    if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) == -1)
        die_safely("tcsetattr");
}

int editor_read_key(void) {
    ssize_t nread;
    char c;
    while ((nread = read(STDIN_FILENO, &c, 1)) != 1) {
        if (nread == -1 && errno != EAGAIN)
            die_safely("read");
    }
    if (c == '\x1b') {
        char seq[3];
        if (read(STDIN_FILENO, &seq[0], 1) != 1) return '\x1b';
        if (read(STDIN_FILENO, &seq[1], 1) != 1) return '\x1b';
        if (seq[0] == '[') {
            if (seq[1] >= '0' && seq[1] <= '9') {
                if (read(STDIN_FILENO, &seq[2], 1) != 1) return '\x1b';
                if (seq[2] == '~') {
                    switch (seq[1]) {
                        case '3':
                            return DEL_KEY;
                        case '5':
                            return PAGE_UP;
                        case '6':
                            return PAGE_DOWN;
                    }
                }
            } else {
                switch (seq[1]) {
                    case 'A':
                        return ARROW_UP;
                    case 'B':
                        return ARROW_DOWN;
                    case 'C':
                        return ARROW_RIGHT;
                    case 'D':
                        return ARROW_LEFT;
                }
            }
        }
    } else {
        return c;
    }
    return 0;
}

int get_cursor_position(size_t *rows, size_t *cols) {
    char buf[32];
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
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == -1 || ws.ws_col == 0) {
        if (write(STDOUT_FILENO, "\x1b[999C\x1b[999B", 12) != 12)
            return -1;
        editor_read_key();
        return get_cursor_position(rows, cols);
    } else {
        *cols = ws.ws_col;
        *rows = ws.ws_row;
        return 0;
    }
}

void read_file_in_editor(FILE *f_in) {
    struct stat stbuf;
    int fd = fileno(f_in);
    if ((fstat(fd, &stbuf) != 0) || (!S_ISREG(stbuf.st_mode))) {
        printf("Cannot open file!\n");
    }
    global_cfg.num_bytes = stbuf.st_size;
    global_cfg.file = mmap(NULL, global_cfg.num_bytes,
                           PROT_READ, MAP_PRIVATE, fd, 0);
    global_cfg.numrows = global_cfg.num_bytes / global_cfg.cur_screencols + 1;
    if (global_cfg.file == MAP_FAILED) {
        die_safely("Map to memory is failed -> read_file_in_editor");
    }
}


void open_file_to_view(char *filename) {
    free(global_cfg.filename);
    global_cfg.filename = strdup(filename);
    global_cfg.fp = fopen(filename, "rb");
    if (ferror(global_cfg.fp))
    {
        exit(EXIT_FAILURE);
    }
    if (!global_cfg.fp)
        die_safely("fopen");
    read_file_in_editor(global_cfg.fp);
}

void append_to_buffer(append_buffer *ab, const char *s, size_t len) {
    char *new = realloc(ab->buffer, ab->len + len);
    if (new == NULL)
        return;
    memcpy(&new[ab->len], s, len);
    ab->buffer = new;
    ab->len += len;
}

void free_append_buffer(append_buffer *ab) {
    free(ab->buffer);
    ab->len = 0;
}

void free_dissasembled_buffer(void) {
    free(global_cfg.dissasembler_buffer);
}

void init_editor(void) {
    global_cfg.dissasembler_mode = MODE_LONG_COMPAT_32;
    global_cfg.mode = TEXT_MODE;
    global_cfg.cx = 0;
    global_cfg.cy = 0;
    global_cfg.rx = 0;
    global_cfg.num_bytes = 0;
    global_cfg.cur_byte = 0;
    global_cfg.numrows = 0;
    global_cfg.rowoff = 0;
    global_cfg.filename = NULL;
    global_cfg.fp = NULL;
    global_cfg.statusmsg[0] = '\0';
    global_cfg.statusmsg_time = 0;

    if (get_window_size(&global_cfg.screenrows,
                        &global_cfg.cur_screencols) == -1) {
        die_safely("getWindowSize");
    }
    if (global_cfg.cur_screencols < SCREENCOLS_MIN) {
        die_safely("Terminal is too short, "
                   "please expand for at least 80 columns!");
    }
    // useful for debug
    //global_cfg.screenrows = 127;
    if (global_cfg.screenrows >= 2)
        global_cfg.screenrows -= 2;
    global_cfg.cur_screencols = SCREENCOLS_MIN;
    global_cfg.screencols = global_cfg.cur_screencols;
    global_cfg.dissasembler_buffer = malloc(global_cfg.screenrows * sizeof(dissasemblerRow));
    atexit(free_dissasembled_buffer);
}
