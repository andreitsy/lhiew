---
name: lhiew-ui-features
description: LHiew keyboard and UI feature contracts — Ctrl-Q/Ctrl-C quitting and Ctrl-Z job control, mode/coordinate recomputation and resize behavior, architecture selection, Page Up/Down paging, F3 hex editing and nibble state, the mandatory <filename>.backup precondition, F8 executable browser prompts, F5 goto, and F7/Shift-F7/n/N search. Read before changing input.c, terminal.c, render.c, hex_edit.c, executable_browser.c or search.c.
---

# LHiew keyboard and UI feature contracts

State named here lives in `editorConfig` (`include/lhiew/types.h`) and is
initialized in `init_editor()`. The canonical cursor is `global_cfg.cur_byte`;
move it and let `switch_mode()` re-derive `cx`/`cy`. See the root `AGENTS.md`
for that invariant and the rendering rules.

## Quitting and job control (Ctrl-Q, Ctrl-C, Ctrl-Z)

`ISIG` stays cleared, so Ctrl-C and Ctrl-Z arrive as the bytes `0x03` and `0x1a`
rather than as signals; the editor decides what they mean. Ctrl-C shares the
Ctrl-Q path, so pending edits still raise `edit_exit_prompt` instead of being
lost, and both are dispatched ahead of view, menu, browser and editing keys but
behind that prompt. Ctrl-Z calls `terminal_suspend`, which restores the saved
termios settings, hands back a cleared terminal with the cursor shown, and
raises `SIGTSTP`; an orphaned process group discards the stop, so the call may
also return at once. On resume it re-enters raw mode with `TCSAFLUSH`, which
discards anything typed while stopped, then re-reads the window size so a resize
made during the stop is applied. Editing state and pending bytes survive a stop.
Every `tcsetattr` goes through `set_terminal_mode`, which retries on `EINTR`:
after `bg`, the background editor is stopped by `SIGTTOU` inside that call and
the call fails once the job is continued, which must not be fatal.

## Modes, coordinates and resize

`editorMode` (`TEXT_MODE`, `HEX_MODE`, `DISASSEMBLER_MODE`) selects which
`draw_row_*` runs. `switch_mode()` recomputes `cur_screencols`, `cy`, `cx`, and
`numrows` from `cur_byte`. Text uses the actual terminal width; hex mode
calculates how many bytes fit alongside offsets and ASCII. Disassembly drops the
raw-byte column on narrow terminals and clips instruction text to fit.

`editor_resize()` records the physical `terminal_rows` and `screencols`, reserves
two rows for status/help, resizes the disassembly buffer, and reflows the cursor.
Below `SCREENCOLS_MIN` columns or `SCREENROWS_MIN` total rows (24x5),
`window_too_small` pauses navigation and displays a resize message. Input
timeouts poll the terminal dimensions, so resizing redraws without a keypress;
enlarging restores the selected byte.

## Architecture selection (Shift-F1, a, o, e)

`disassemblerMode` (`REAL`, `MODE_LONG_COMPAT_16/32/64`) controls x86 decoding;
`architecture` and `big_endian` select the wider CPU profile. `init_editor()`
defaults to x86-32. File headers override those defaults; raw files retain the
x86 fallback, while unsupported/malformed headers select `ARCH_UNKNOWN` and
render bytes. `architecture_select()` clears cached rows without moving
`cur_byte`; selecting Auto re-detects the header (raw resets to x86-32).

Shift-F1 or `a` opens the scrollable architecture menu. `architecture_menu` and
`architecture_choice` hold its state; Escape cancels, Enter applies. `o` cycles
x86 modes via the same profile API. `e` jumps to the detected entry/first code
region; entering assembly mode at offset zero also performs that jump. The menu
uses the normal append buffer.

The left gutter remains a file offset. Instruction formatting uses the mapped
runtime address for supported containers; raw/unmapped bytes use file offsets.
Native relative operand syntax, including RISC-V branch displacements, is
retained. No relocations, symbols, universal-binary slices, or mixed ARM mapping
symbols are applied.

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
