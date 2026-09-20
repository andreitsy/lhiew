---
name: lhiew-ui-features
description: LHiew keyboard and UI feature contracts — Page Up/Down paging, F3 hex editing and nibble state, the mandatory <filename>.backup precondition, F8 executable browser prompts, F5 goto, and F7/Shift-F7/n/N search. Read before changing input.c, render.c, hex_edit.c, executable_browser.c or search.c.
---

# LHiew keyboard and UI feature contracts

State named here lives in `editorConfig` (`include/lhiew/types.h`) and is
initialized in `init_editor()`. The canonical cursor is `global_cfg.cur_byte`;
move it and let `switch_mode()` re-derive `cx`/`cy`. See the root `AGENTS.md`
for that invariant and the rendering rules.

## Paging

Page Up/Down moves by `screenrows` instructions in disassembly, preserving the byte offset within the destination instruction where it fits. Text and hex paging moves by `screenrows * cur_screencols` bytes. All modes clamp paging at file boundaries.

## Hex editing (F3)

F3 enters hex editing from any view. `editing`, `edit_ascii` and `edit_nibble`
track the edit session, input pane and nibble. Each typed digit updates the
private mapped byte immediately; after two digits the cursor advances. Tab
switches to printable ASCII. F9 saves; Escape/F10 leaves edit mode. Leaving or
quitting with pending changes opens `edit_exit_prompt` (1 = leave, 2 = quit):
`s` saves, `d` discards, Escape continues. Mode/architecture shortcuts are
inactive while editing so letters remain data. Editing never resizes the file.

## Backup precondition

Before editing begins, `<filename>.backup` must exist as an independent regular
file. A new backup captures the original bytes; existing backups are preserved
across saves and sessions. Failure to copy, validate or flush the backup prevents
editing. Backups preserve file bytes, not all metadata, and are not automatically
restored after a failed save.

## Executable browser (F8)

F8 or `b` while viewing opens the executable browser. `executable_browser`,
`executable_imports` and `executable_choice` track its state; Tab switches the
import and header/region lists, Enter jumps to a raw file span, and F3 opens a
field prompt. `executable_prompt` (1 = name, 2 = ordinal), `executable_input` and
`executable_input_length` hold prompt state. Names keep their exact byte length;
ordinal edits preserve field width and encoding flags. F9 saves; Escape cancels
a prompt or closes the browser, retaining staged edits for the ordinary hex
save/discard flow. F8 remains available while editing; `b` remains byte input.
Browser text is copied from bounded metadata, sanitized and drawn through the
append buffer. No table resizing, import insertion/removal or relocation occurs.

## Goto (F5)

F5 (or `g` while viewing) opens `goto_prompt`; `goto_input`/`goto_length` hold an
absolute hexadecimal file offset, with optional `0x`. Parsing checks overflow
and file bounds. Home/End select row boundaries; Ctrl-Home/End select the first
and last bytes. One-past-EOF is a valid viewing cursor but cannot be edited.
Prompts and edit cursors use the normal append-buffer rendering path. When an
edit-session file conflict is detected, rendering avoids the stale mapping.
The build requests 64-bit `off_t`; mappings still require enough virtual address
space, and pending changes require memory proportional to bytes/pages touched.

## Search (F7)

F7 (or `s` while viewing) opens `search_prompt`. Tab switches between hexadecimal
pairs and literal text, Ctrl-U clears, and Enter compiles the pattern into
`search_pattern`/`search_pattern_length` and searches from the cursor. Shift-F7
repeats in the recorded direction; `n`/`N` repeat forward/backward while viewing
and set that direction. Repeats start one byte away from the cursor in the chosen
direction so the current match is not returned again. Searches do not wrap.
Search works during an edit session, where it
reads the private mapping and therefore matches pending changes; `F7` is used
there because letters are data. A scan is a single uninterruptible pass, so add a
progress/abort path before relaxing the pattern length or adding wildcards.
