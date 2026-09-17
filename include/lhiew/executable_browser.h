#pragma once

#include "lhiew/append_buffer.h"

void executable_browser_open(void);
void executable_browser_keypress(int key);
void executable_browser_draw(append_buffer *ab);
void executable_browser_close(void);
const char *executable_browser_message(void);
