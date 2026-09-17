"""Drive byte and text search through a real terminal.

Usage: python3 tests/test_search_ui.py build/lhiew
Covers the search steps of the Hiew workflow scenarios: locating a signature,
repeating forward and backward, and finding the byte pairs a damaged file
repair would target.
"""

import argparse
from pathlib import Path
import re
import tempfile
import unittest

from test_terminal_resize import Viewer


F7 = b"\x1b[18~"
SHIFT_F7 = b"\x1b[18;2~"
F3 = b"\x1bOR"
ESC = b"\x1b"
TAB = b"\t"


class SearchTests(unittest.TestCase):
    def setUp(self):
        self.directory = tempfile.TemporaryDirectory(prefix="lhiew-search-pty-")
        self.addCleanup(self.directory.cleanup)
        self.fixture = Path(self.directory.name) / "damaged.bin"
        # Two CRLF pairs plus a PK signature, as in the repair and format scenarios.
        self.payload = (b"\x89PNG\x0d\x0a\x1a\x0a" + b"A" * 16 + b"\x0d\x0a"
                        + b"PK\x03\x04" + b"B" * 16 + b"\x0d\x0a")
        self.fixture.write_bytes(self.payload)

    def viewer(self, columns=80, rows=24):
        return Viewer(self.binary, self.fixture, columns, rows)

    def open_prompt(self, viewer, key=F7):
        viewer.press(key)
        return viewer.expect(lambda frame: frame.lines[0].startswith("Search "),
                             "search prompt")

    def expect_found(self, viewer, offset):
        def matches(frame):
            return (re.search(rf"\bfound\b.*\b{offset:x}\b", frame.lines[-1].lower())
                    and re.search(rf"\b{offset}:{len(self.payload)}\b", frame.lines[-2]))
        return viewer.expect(matches, f"match reported at {offset:#x}")

    def search(self, viewer, text, key=F7):
        self.open_prompt(viewer, key)
        viewer.press(text.encode("ascii") + b"\r")

    def test_hex_search_finds_first_match_from_cursor(self):
        with self.viewer() as viewer:
            viewer.expect_mode("text", 0, len(self.payload))
            self.search(viewer, "0d0a")
            self.expect_found(viewer, self.payload.index(b"\x0d\x0a"))

    def test_repeat_moves_forward_then_backward(self):
        offsets = [match.start() for match in re.finditer(b"\x0d\x0a", self.payload)]
        self.assertGreaterEqual(len(offsets), 3)
        with self.viewer() as viewer:
            viewer.expect_mode("text", 0, len(self.payload))
            self.search(viewer, "0d 0a")
            self.expect_found(viewer, offsets[0])
            for offset in offsets[1:]:
                viewer.press(b"n")
                self.expect_found(viewer, offset)
            # A repeat past the final match leaves the cursor where it was.
            viewer.press(b"n")
            viewer.expect(lambda frame: "not found" in frame.lines[-1].lower(),
                          "exhausted forward search")
            for offset in reversed(offsets[:-1]):
                viewer.press(b"N")
                self.expect_found(viewer, offset)

    def test_shift_f7_repeats_in_the_recorded_direction(self):
        offsets = [match.start() for match in re.finditer(b"\x0d\x0a", self.payload)]
        with self.viewer() as viewer:
            viewer.expect_mode("text", 0, len(self.payload))
            self.search(viewer, "0d0a")
            self.expect_found(viewer, offsets[0])
            viewer.press(SHIFT_F7)
            self.expect_found(viewer, offsets[1])

    def test_text_pane_searches_literal_bytes(self):
        with self.viewer() as viewer:
            viewer.expect_mode("text", 0, len(self.payload))
            self.open_prompt(viewer)
            viewer.press(TAB)
            viewer.expect(lambda frame: frame.lines[0].startswith("Search text"),
                          "text input pane")
            viewer.press(b"PK\r")
            self.expect_found(viewer, self.payload.index(b"PK"))

    def test_incomplete_hex_is_rejected_without_moving(self):
        with self.viewer() as viewer:
            viewer.expect_mode("text", 0, len(self.payload))
            self.open_prompt(viewer)
            viewer.press(b"0d0\r")
            viewer.expect(lambda frame: "pair" in frame.lines[-1].lower(),
                          "incomplete byte rejected")
            # The prompt stays open so the pattern can be corrected in place.
            viewer.press(b"a\r")
            self.expect_found(viewer, self.payload.index(b"\x0d\x0a"))

    def test_missing_pattern_reports_and_keeps_position(self):
        with self.viewer() as viewer:
            viewer.expect_mode("text", 0, len(self.payload))
            self.search(viewer, "deadbeef")
            viewer.expect(
                lambda frame: "not found" in frame.lines[-1].lower()
                and re.search(rf"\b0:{len(self.payload)}\b", frame.lines[-2]),
                "unmatched pattern leaves the cursor at 0")

    def test_escape_cancels_without_searching(self):
        with self.viewer() as viewer:
            viewer.expect_mode("text", 0, len(self.payload))
            self.open_prompt(viewer)
            viewer.press(b"0d0a")
            viewer.press(ESC)
            viewer.expect_mode("text", 0, len(self.payload))

    def test_search_reaches_matches_while_editing(self):
        with self.viewer() as viewer:
            viewer.expect_mode("text", 0, len(self.payload))
            viewer.press(F3)
            viewer.expect(lambda frame: "edit hex" in frame.lines[-2].lower(),
                          "hex editing session")
            self.search(viewer, "504b")
            viewer.expect(
                lambda frame: re.search(rf"\b{self.payload.index(b'PK')}:", frame.lines[-2])
                and "edit hex" in frame.lines[-2].lower(),
                "match reached without leaving the editor")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("binary", type=Path)
    parser.add_argument("arguments", nargs="*")
    options = parser.parse_args()
    SearchTests.binary = options.binary.resolve()
    unittest.main(argv=["test_search_ui.py"] + options.arguments)


if __name__ == "__main__":
    main()
