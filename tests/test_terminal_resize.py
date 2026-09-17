"""Exercise real terminal geometry without third-party terminal libraries.

Usage: python3 tests/test_terminal_resize.py build/lhiew [--snapshot-dir DIR]
The small ANSI emulator checks delayed autowrap, scrolling, and cursor bounds.
"""

import argparse
import errno
import fcntl
import os
from pathlib import Path
import re
import select
import struct
import subprocess
import tempfile
import termios
import time
import unittest


FRAME_START = b"\x1b[?25l\x1b[H"
FRAME_END = re.compile(rb"\x1b\[[0-9]+;[0-9]+H(?:\x1b\[\?25h)?$")
CSI = re.compile(r"\x1b\[([0-?]*)([ -/]*)([@-~])")


class Screen:
    """The ANSI operations emitted by LHiew, including delayed line wrapping."""

    def __init__(self, columns, rows, data):
        self.columns = columns
        self.rows = rows
        self.cells = [[" "] * columns for _ in range(rows)]
        self.x = self.y = 0
        self.wrap_pending = False
        self.errors = []
        self.parse(data.decode("ascii", errors="replace"))

    @property
    def lines(self):
        return ["".join(row) for row in self.cells]

    def dump(self):
        return "\n".join("|" + line + "|" for line in self.lines)

    def linefeed(self):
        self.wrap_pending = False
        self.y += 1
        if self.y >= self.rows:
            self.errors.append("screen scrolled past the bottom row")
            self.cells.pop(0)
            self.cells.append([" "] * self.columns)
            self.y = self.rows - 1

    def parse(self, text):
        position = 0
        while position < len(text):
            character = text[position]
            if character == "\x1b":
                match = CSI.match(text, position)
                if match is None:
                    self.errors.append("incomplete or unsupported escape sequence")
                    return
                arguments, _, command = match.groups()
                position = match.end()
                if command == "m" or (arguments == "?25" and command in "hl"):
                    continue
                numbers = [int(value or "0") for value in arguments.split(";")]
                if command in "Hf":
                    row = numbers[0] or 1
                    column = (numbers[1] if len(numbers) > 1 else 1) or 1
                    if not (1 <= row <= self.rows and 1 <= column <= self.columns):
                        self.errors.append(f"cursor outside screen: {column}x{row}")
                    self.y = min(max(row - 1, 0), self.rows - 1)
                    self.x = min(max(column - 1, 0), self.columns - 1)
                    self.wrap_pending = False
                elif command == "K":
                    mode = numbers[0]
                    start = 0 if mode in (1, 2) else self.x
                    end = self.x + 1 if mode == 1 else self.columns
                    self.cells[self.y][start:end] = [" "] * (end - start)
                elif command == "J" and numbers[0] == 2:
                    self.cells = [[" "] * self.columns for _ in range(self.rows)]
                else:
                    self.errors.append(f"unsupported CSI: {arguments}{command}")
                continue
            position += 1
            if character == "\r":
                self.x = 0
                self.wrap_pending = False
            elif character == "\n":
                self.linefeed()
            elif character in "\x00\x07":
                continue
            elif ord(character) >= 32 and character != "\x7f":
                if self.wrap_pending:
                    self.errors.append("physical line overflow triggered autowrap")
                    self.x = 0
                    self.linefeed()
                self.cells[self.y][self.x] = character
                if self.x == self.columns - 1:
                    self.wrap_pending = True
                else:
                    self.x += 1
            else:
                self.errors.append(f"unexpected control character: {ord(character)}")


class Viewer:
    def __init__(self, binary, filename, columns, rows):
        self.columns, self.rows = columns, rows
        self.buffer = b""
        self.latest = None
        self.master, slave = os.openpty()
        self.set_size(slave, columns, rows)
        command = [str(binary)] + ([str(filename)] if filename is not None else [])
        try:
            self.process = subprocess.Popen(
                command, stdin=slave, stdout=slave, stderr=slave,
                env=dict(os.environ, TERM="xterm-256color"), close_fds=True,
            )
        finally:
            os.close(slave)
        os.set_blocking(self.master, False)

    @staticmethod
    def set_size(descriptor, columns, rows):
        fcntl.ioctl(descriptor, termios.TIOCSWINSZ, struct.pack("HHHH", rows, columns, 0, 0))

    def __enter__(self):
        return self

    def __exit__(self, *_):
        try:
            if self.process.poll() is None:
                self.press(b"\x11")
                try:
                    self.wait_for_exit()
                except subprocess.TimeoutExpired:
                    self.process.kill()
                    self.process.wait(timeout=1)
        finally:
            os.close(self.master)

    def press(self, keys):
        os.write(self.master, keys)

    def read_frames(self, timeout):
        if select.select([self.master], [], [], timeout)[0]:
            while True:
                try:
                    chunk = os.read(self.master, 65536)
                except BlockingIOError:
                    break
                except OSError as error:
                    if error.errno == errno.EIO:
                        break
                    raise
                if not chunk:
                    break
                self.buffer += chunk
        frames = []
        while True:
            start = self.buffer.find(FRAME_START)
            if start < 0:
                break
            next_start = self.buffer.find(FRAME_START, start + len(FRAME_START))
            if next_start >= 0:
                end = next_start
            else:
                match = FRAME_END.search(self.buffer, start + len(FRAME_START))
                if match is None:
                    break
                end = match.end()
            data = self.buffer[start:end]
            self.buffer = self.buffer[end:]
            self.latest = Screen(self.columns, self.rows, data)
            frames.append(self.latest)
        return frames

    def expect(self, predicate, description, allow_old_geometry=False):
        deadline = time.monotonic() + 0.8
        while time.monotonic() < deadline:
            for frame in self.read_frames(0.02):
                if frame.errors and not allow_old_geometry:
                    raise AssertionError("; ".join(frame.errors) + "\n" + frame.dump())
                if not frame.errors and predicate(frame):
                    return frame
            if self.process.poll() is not None:
                raise AssertionError(f"viewer exited with {self.process.returncode}: {description}")
        details = self.latest.dump() if self.latest is not None else repr(self.buffer)
        errors = self.latest.errors if self.latest is not None else []
        raise AssertionError(f"Timed out waiting for {description}; {errors}\n{details}")

    def expect_mode(self, mode, offset, total, allow_old_geometry=False, operand=None):
        def matches(frame):
            status = frame.lines[-2].lower()
            mode_matches = mode in status or (mode == "asm" and "disass" in status)
            if operand is not None:
                operand_label = operand.lower()
                mode_matches = mode_matches and (
                    re.search(rf"\basm{operand_label}\b", status)
                    or f"disassembler {operand_label}-bit" in status
                )
            return mode_matches and re.search(rf"\b{offset}:{total}\b", status)
        return self.expect(matches, f"{mode} at {offset}:{total}", allow_old_geometry)

    def expect_warning(self, allow_old_geometry=False):
        def matches(frame):
            warning = "\n".join(frame.lines)
            if self.columns < 12:
                return bool(warning.strip())
            return "24x5" in warning and re.search(r"ctrl-q|\^q", warning.lower())
        return self.expect(matches, "bounded resize warning", allow_old_geometry)

    def resize(self, columns, rows):
        # Drain completed old-size frames before changing the kernel's window size.
        self.read_frames(0)
        self.set_size(self.master, columns, rows)
        self.columns, self.rows = columns, rows

    def read_for(self, seconds):
        deadline = time.monotonic() + seconds
        while time.monotonic() < deadline:
            for frame in self.read_frames(max(0, min(0.02, deadline - time.monotonic()))):
                if frame.errors:
                    raise AssertionError("; ".join(frame.errors) + "\n" + frame.dump())

    def quit(self):
        self.press(b"\x11")
        self.wait_for_exit()
        if self.process.returncode != 0:
            raise AssertionError(f"Ctrl-Q exit status: {self.process.returncode}")

    def wait_for_exit(self):
        # TCSAFLUSH can wait for pending terminal output to drain on macOS.
        deadline = time.monotonic() + 1
        while self.process.poll() is None:
            self.read_frames(0.01)
            if time.monotonic() >= deadline:
                raise subprocess.TimeoutExpired(self.process.args, 1)


class TerminalResizeTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.directory = tempfile.TemporaryDirectory(prefix="lhiew-pty-")
        cls.fixture = Path(cls.directory.name) / "nops.bin"
        cls.fixture.write_bytes(b"\x90" * 257)
        cls.large = Path(cls.directory.name) / "large.bin"
        cls.large.write_bytes(b"\x90" * 4097)
        cls.mixed = Path(cls.directory.name) / "mixed.bin"
        cls.mixed.write_bytes(b"\xb8\x78\x56\x34\x12\x90" * 100)
        cls.empty = Path(cls.directory.name) / "empty.bin"
        cls.empty.write_bytes(b"")
        cls.short = Path(cls.directory.name) / "short.bin"
        cls.short.write_bytes(b"\x90" * 7)

    @classmethod
    def tearDownClass(cls):
        cls.directory.cleanup()

    def snapshot(self, label, frame):
        if self.snapshot_dir:
            self.snapshot_dir.mkdir(parents=True, exist_ok=True)
            (self.snapshot_dir / f"{label}.txt").write_text(frame.dump() + "\n")

    def assert_centered_nop(self, frame, offset):
        content_rows = frame.rows - 2
        selected_row = min(offset, content_rows // 2)
        first_offset = offset - selected_row
        for row, line in enumerate(frame.lines[:content_rows]):
            address = first_offset + row
            if address < 257:
                self.assertTrue(line.startswith(f"{address:08x}"), frame.dump())
                self.assertRegex(line.lower(), r"\bnop\b", frame.dump())
        self.assertTrue(frame.lines[selected_row].startswith(f"{offset:08x}"), frame.dump())

    def test_startup_sizes_and_modes(self):
        for columns, rows in ((24, 5), (40, 10), (80, 24), (120, 40)):
            with self.subTest(size=(columns, rows)):
                with Viewer(self.binary, self.fixture, columns, rows) as viewer:
                    for mode in ("text", "hex", "asm"):
                        frame = viewer.expect_mode(mode, 0, 257)
                        self.snapshot(f"startup-{columns}x{rows}-{mode}", frame)
                        if mode == "text":
                            self.assertEqual(len(frame.lines[0].rstrip()), columns)
                        if mode == "asm":
                            self.assert_centered_nop(frame, 0)
                        if mode != "asm":
                            viewer.press(b"m")
                    viewer.quit()

    def test_idle_resize_preserves_cursor_in_all_modes(self):
        with Viewer(self.binary, self.fixture, 80, 24) as viewer:
            viewer.expect_mode("text", 0, 257)
            viewer.press(b"l" * 17)
            viewer.expect_mode("text", 17, 257)
            for mode in ("text", "hex", "asm"):
                for columns, rows in ((24, 5), (120, 40), (40, 10), (80, 24)):
                    viewer.resize(columns, rows)
                    frame = viewer.expect_mode(mode, 17, 257, allow_old_geometry=True)
                    self.snapshot(f"resized-{columns}x{rows}-{mode}", frame)
                    if mode == "asm":
                        self.assert_centered_nop(frame, 17)
                for columns, rows in ((1, 1), (12, 3), (24, 4)):
                    viewer.resize(columns, rows)
                    frame = viewer.expect_warning(allow_old_geometry=True)
                    if columns == 12:
                        # Consume queued input while the window is too small.
                        viewer.press(b"lljjhhkkmo\x1b[5~\x1b[6~")
                        viewer.read_for(0.16)
                        frame = viewer.latest
                    self.snapshot(f"too-small-{columns}x{rows}-{mode}", frame)
                viewer.resize(80, 24)
                viewer.expect_mode(mode, 17, 257, allow_old_geometry=True,
                                   operand="32" if mode == "asm" else None)
                if mode != "asm":
                    viewer.press(b"m")
                    viewer.expect_mode("hex" if mode == "text" else "asm", 17, 257)
            viewer.quit()

    def test_assembler_centering_and_page_navigation(self):
        for mode_keys in (b"mm", b"\r"):
            with self.subTest(mode_keys=mode_keys):
                with Viewer(self.binary, self.fixture, 80, 24) as viewer:
                    viewer.expect_mode("text", 0, 257)
                    viewer.press(b"l" * 73 + mode_keys)
                    frame = viewer.expect_mode("asm", 73, 257)
                    self.assert_centered_nop(frame, 73)
                    for key, offset in ((b"\x1b[B", 74), (b"\x1b[A", 73)):
                        viewer.press(key)
                        frame = viewer.expect_mode("asm", offset, 257)
                        self.assert_centered_nop(frame, offset)
                    for columns, rows in ((24, 5), (120, 40), (40, 10)):
                        viewer.resize(columns, rows)
                        frame = viewer.expect_mode("asm", 73, 257, allow_old_geometry=True)
                        self.assert_centered_nop(frame, 73)
                        page_rows = rows - 2
                        for key, offset in ((b"\x1b[6~", 73 + page_rows),
                                            (b"\x1b[5~", 73)):
                            viewer.press(key)
                            frame = viewer.expect_mode("asm", offset, 257)
                            self.assert_centered_nop(frame, offset)
                        self.snapshot(f"centered-{columns}x{rows}-asm", frame)
                    viewer.quit()

    def test_page_keys_move_one_screen_in_each_mode(self):
        modes = (("text", b"", 320), ("hex", b"m", 32), ("asm", b"\r", 8))
        for mode, mode_keys, page_bytes in modes:
            with self.subTest(mode=mode):
                with Viewer(self.binary, self.large, 40, 10) as viewer:
                    viewer.expect_mode("text", 0, 4097)
                    viewer.press(b"l" * 7 + mode_keys)
                    viewer.expect_mode(mode, 7, 4097)
                    for key, offset in ((b"\x1b[6~", 7 + page_bytes),
                                        (b"\x1b[6~", 7 + 2 * page_bytes),
                                        (b"\x1b[5~", 7 + page_bytes),
                                        (b"\x1b[5~", 7),
                                        (b"\x1b[5~", 0),
                                        (b"\x1b[5~", 0)):
                        viewer.press(key)
                        viewer.expect_mode(mode, offset, 4097)
                    viewer.quit()

    def test_assembler_pages_follow_mixed_instruction_lengths(self):
        with Viewer(self.binary, self.mixed, 40, 10) as viewer:
            viewer.expect_mode("text", 0, 600)
            # Start two bytes into a five-byte mov; each following nop is one byte.
            viewer.press(b"l" * 8 + b"\r")
            viewer.expect_mode("asm", 8, 600)
            for key, offset in ((b"\x1b[6~", 32), (b"\x1b[6~", 56),
                                (b"\x1b[5~", 32), (b"\x1b[5~", 8)):
                viewer.press(key)
                frame = viewer.expect_mode("asm", offset, 600)
                selected_row = min((offset // 6) * 2, 4)
                self.assertTrue(frame.lines[selected_row].startswith(f"{offset - 2:08x}"),
                                frame.dump())
                self.assertRegex(frame.lines[selected_row].lower(), r"\bmov\b", frame.dump())
            viewer.quit()

    def test_empty_no_file_and_eof(self):
        for filename, total in ((None, 0), (self.empty, 0), (self.short, 7)):
            with self.subTest(filename=filename):
                with Viewer(self.binary, filename, 24, 5) as viewer:
                    viewer.expect_mode("text", 0, total)
                    if total:
                        viewer.press(b"l" * total)
                        viewer.expect_mode("text", total, total)
                    for mode in ("text", "hex", "asm"):
                        for columns, rows in ((120, 40), (24, 5)):
                            viewer.resize(columns, rows)
                            viewer.expect_mode(mode, total, total, allow_old_geometry=True)
                        if mode != "asm":
                            viewer.press(b"m")
                            viewer.expect_mode("hex" if mode == "text" else "asm", total, total)
                    viewer.quit()

    def test_eof_navigation_and_page_bounds(self):
        with Viewer(self.binary, self.short, 80, 24) as viewer:
            viewer.expect_mode("text", 0, 7)
            for key, offset in ((b"\x1b[6~", 7), (b"\x1b[6~", 7),
                                (b"\x1b[5~", 0), (b"\x1b[5~", 0)):
                viewer.press(key)
                viewer.expect_mode("text", offset, 7)
            viewer.press(b"mm" + b"l" * 7)
            frame = viewer.expect_mode("asm", 7, 7)
            self.assertTrue(all(line.strip() == "~" for line in frame.lines[:-2]), frame.dump())
            for key, offset in ((b"l", 7), (b"j", 7), (b"k", 6),
                                (b"h", 5), (b"l", 6), (b"l", 7),
                                (b"\x1b[5~", 0), (b"\x1b[5~", 0),
                                (b"\x1b[6~", 7), (b"\x1b[6~", 7),
                                (b"h" * 8, 0), (b"k", 0), (b"l", 1)):
                viewer.press(key)
                viewer.expect_mode("asm", offset, 7)
            viewer.quit()

    def test_binary_fixtures_and_operand_modes(self):
        fixtures = Path(__file__).resolve().parent / "fixtures"
        cases = (
            ("raw_x86_16.bin", "16R", 2, 0,
             ((0, "mov"), (3, "inc"), (4, "add")), "%ax"),
            ("raw_x86_16.bin", "16", 3, 0,
             ((0, "mov"), (3, "inc"), (4, "add")), "%ax"),
            ("raw_x86_32.bin", "32", 0, 0,
             ((0, "mov"), (5, "add"), (8, "xor")), "%eax"),
            ("raw_x86_64.bin", "64", 1, 0,
             ((0, "mov"), (5, "mov"), (10, "syscall")), "%eax"),
            ("minimal_exit_x86_64.elf", "64", 1, 120,
             ((120, "mov"), (125, "mov"), (130, "syscall")), "%eax"),
            ("invalid_truncated.bin", "64", 1, 0,
             ((0, "db"), (1, "nop"), (2, "db")), "db 06"),
            ("empty.bin", "64", 1, 0, (), None),
        )
        for name, operand, cycles, offset, instructions, wide_operand in cases:
            with self.subTest(fixture=name, operand=operand):
                filename = fixtures / name
                total = filename.stat().st_size
                with Viewer(self.binary, filename, 24, 5) as viewer:
                    initial = viewer.expect_mode("text", 0, total)
                    if name.endswith(".elf"):
                        self.assertTrue(initial.lines[0].startswith("@ELF"), initial.dump())
                        viewer.press(b"m")
                        header = viewer.expect_mode("hex", 0, total)
                        self.assertIn("7f 45", header.lines[0])
                        self.assertIn("4c 46", header.lines[1])
                        viewer.resize(80, 24)
                        header = viewer.expect_mode("hex", 0, total, allow_old_geometry=True)
                        self.assertIn("7f 45 4c 46", header.lines[0])
                        self.snapshot("fixture-elf-header-80x24-hex", header)
                        viewer.resize(24, 5)
                        viewer.expect_mode("hex", 0, total, allow_old_geometry=True)
                        viewer.press(b"\r")
                        viewer.expect_mode("text", 0, total)
                    viewer.press(b"l" * offset + b"mm" + b"o" * cycles)
                    frame = viewer.expect_mode("asm", offset, total, operand=operand)
                    for columns, rows in ((24, 5), (80, 24)):
                        if columns != 24:
                            viewer.resize(columns, rows)
                            frame = viewer.expect_mode("asm", offset, total,
                                                       allow_old_geometry=True, operand=operand)
                        selected_row = 0
                        if instructions:
                            first_address = f"{instructions[0][0]:08x}"
                            matches = [row for row, line in enumerate(frame.lines[:-2])
                                       if line.startswith(first_address)]
                            self.assertEqual(len(matches), 1, frame.dump())
                            selected_row = matches[0]
                        visible_instructions = instructions[:rows - 2 - selected_row]
                        for line_number, (address, mnemonic) in enumerate(visible_instructions):
                            line = frame.lines[selected_row + line_number].lower()
                            self.assertTrue(line.startswith(f"{address:08x}"), frame.dump())
                            self.assertRegex(line, rf"\b{mnemonic}\b", frame.dump())
                        if columns == 80 and wide_operand:
                            self.assertIn(wide_operand, frame.lines[selected_row].lower())
                        self.snapshot(f"fixture-{name}-{operand}-{columns}x{rows}", frame)
                    viewer.quit()

    def test_start_too_small_and_quit(self):
        for columns, rows in ((1, 1), (12, 3), (24, 4)):
            with self.subTest(size=(columns, rows)):
                with Viewer(self.binary, self.fixture, columns, rows) as viewer:
                    viewer.expect_warning()
                    viewer.quit()


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("binary", type=Path)
    parser.add_argument("--snapshot-dir", type=Path)
    options = parser.parse_args()
    TerminalResizeTests.binary = options.binary.resolve()
    TerminalResizeTests.snapshot_dir = options.snapshot_dir
    unittest.main(argv=[__file__], verbosity=2)
