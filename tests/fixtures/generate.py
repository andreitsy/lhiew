"""Regenerate deterministic, assembler-free binary fixtures; --check only verifies."""

import argparse
from pathlib import Path
import struct


def fixtures():
    # Intel syntax: mov ax,0x1234; inc ax; add ax,1; ret.
    code16 = bytes.fromhex("b8 34 12 40 83 c0 01 c3")
    # Intel syntax: mov eax,0x12345678; add eax,1; xor ebx,ebx; ret.
    code32 = bytes.fromhex("b8 78 56 34 12 83 c0 01 31 db c3")
    # Linux x86-64 exit(42): mov eax,60; mov edi,42; syscall.
    code64 = bytes.fromhex("b8 3c 00 00 00 bf 2a 00 00 00 0f 05")
    code_offset = 64 + 56
    image_base = 0x400000
    image_size = code_offset + len(code64)
    identifier = b"\x7fELF\x02\x01\x01" + bytes(9)
    header = struct.pack(
        "<16sHHIQQQIHHHHHH", identifier, 2, 62, 1,
        image_base + code_offset, 64, 0, 0, 64, 56, 1, 0, 0, 0,
    )
    # One PT_LOAD segment, read/execute, with no section table or interpreter.
    segment = struct.pack(
        "<IIQQQQQQ", 1, 5, 0, image_base, image_base,
        image_size, image_size, 0x1000,
    )
    return {
        "raw_x86_16.bin": code16,
        "raw_x86_32.bin": code32,
        "raw_x86_64.bin": code64,
        "minimal_exit_x86_64.elf": header + segment + code64,
        "empty.bin": b"",
        # PUSH ES is invalid in 64-bit mode; E8 lacks its call displacement.
        "invalid_truncated.bin": bytes.fromhex("06 90 e8"),
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--check", action="store_true")
    args = parser.parse_args()
    directory = Path(__file__).resolve().parent
    mismatches = []
    for name, expected in fixtures().items():
        path = directory / name
        if args.check:
            if not path.exists() or path.read_bytes() != expected:
                mismatches.append(name)
        else:
            path.write_bytes(expected)
    if mismatches:
        parser.exit(1, "Fixture mismatch: " + ", ".join(mismatches) + "\n")
    print("Binary fixtures verified." if args.check else "Binary fixtures regenerated.")


if __name__ == "__main__":
    main()
