#pragma once

#include <stdio.h>

void open_file_to_view(const char *filename);
void read_file_in_editor(FILE *f_in);
/* Replace the view with the current descriptor contents; preserve the old view
   on error and return zero with errno set. Does not refresh architecture data. */
int reload_file_in_editor(void);
