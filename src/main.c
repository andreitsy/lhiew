#include "lhiew/editor.h"
#include "lhiew/file_buffer.h"
#include "lhiew/input.h"
#include "lhiew/render.h"
#include "lhiew/terminal.h"
#include "lhiew/types.h"

int main(int argc, char *argv[]) {
    enable_raw_mode();
    init_editor();
    if (argc >= 2) {
        open_file_to_view(argv[1]);
    }
    editor_set_status_message(HELLO_MESSAGE);
    while (1) {
        editor_refresh_screen();
        editor_process_keypress();
    }
}
