#ifndef OTUS_PROJECT_INPUT_KEYBOARD_H
#define OTUS_PROJECT_INPUT_KEYBOARD_H

#include "output.h"
#define CTRL_KEY(k) ((k) & 0x1f)

void editor_move_cursor(int key);
void editor_process_keypress(void);

#endif //OTUS_PROJECT_INPUT_KEYBOARD_H
