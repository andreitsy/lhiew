#pragma once

#include <stddef.h>
#include <stdint.h>

/* Editing overwrites existing bytes in a private mapping. Nonzero means success.
   Save failures retain pending edits, but writes already made are not rolled back.
   Cancel reloads the current disk contents; on reload failure editing stays open. */
int hex_edit_begin(void);
/* Before reading mapped bytes, detect external replacement/truncation while
   editing. Returns one outside an edit session or when the file is unchanged. */
int hex_edit_check_file(void);
int hex_edit_set_byte(size_t offset, uint8_t value);
typedef struct hexPatch {
    size_t offset;
    const uint8_t *bytes;
    size_t length;
} hexPatch;
/* Stage all spans or change nothing. Empty spans may have NULL bytes; an empty
   list may be NULL. Later overlapping spans take precedence. Source byte arrays
   must remain unchanged throughout the call (do not alias the edited mapping). */
int hex_edit_patch(const hexPatch *patches, size_t count);
int hex_edit_save(void);
void hex_edit_cancel(void);
size_t hex_edit_dirty_count(void);
