#include "editor.h"
#include "data.h"
#include "input_keyboard.h"
#include "output.h"

editorConfig global_cfg;

int main(int argc, char *argv[]) {
    enable_raw_mode();
    init_editor();
    if (argc >= 2) {
        open_file_to_view(argv[1]);
    }
    editor_set_status_message(HELLO_MESSAGE);
#pragma clang diagnostic push
#pragma ide diagnostic ignored "EndlessLoop"
    while (1) {
        editor_refresh_screen();
        editor_process_keypress();
    }
#pragma clang diagnostic pop
}
