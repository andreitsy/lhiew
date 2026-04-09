#pragma once

#define CTRL_KEY(k) ((k) & 0x1f)

void editor_move_cursor(int key);
void editor_process_keypress(void);
