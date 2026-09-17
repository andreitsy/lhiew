#pragma once

/* Keep the first independent <filename>.backup. New backups are copied before
   publication; existing backups are never replaced. Nonzero means success. */
int file_backup_ensure(const char *filename, int source_fd);
