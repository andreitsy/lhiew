"""Small, independently serialized executable files for import-browser PTY tests.

The layouts follow the format references in docs/executable-imports.md. These are metadata
fixtures with actual code/data extents, not files intended to execute.
Run this file with a directory argument to write all six examples there.
"""

from dataclasses import dataclass
from pathlib import Path
import struct


def u16(data, offset, value):
    struct.pack_into("<H", data, offset, value)


def u32(data, offset, value):
    struct.pack_into("<I", data, offset, value)


@dataclass
class ImportFixture:
    format: str
    data: bytes
    module: bytes
    new_module: bytes
    module_offset: int
    module_record: int
    symbol: bytes
    new_symbol: bytes
    symbol_offset: int
    symbol_record: int
    symbol_choice: int
    ordinal_choice: int | None
    ordinal_offset: int | None
    ordinal_width: int = 0
    ordinal_max: int = 0
    ordinal_flags: int = 0
    mirror_offset: int | None = None
    header_offset: int = 0x80

    def expected(self, module=False, symbol=False, ordinal=None):
        result = bytearray(self.data)
        if module:
            result[self.module_offset:self.module_offset + len(self.module)] = self.new_module
        if symbol:
            result[self.symbol_offset:self.symbol_offset + len(self.symbol)] = self.new_symbol
        if ordinal is not None:
            encoded = (ordinal | self.ordinal_flags).to_bytes(self.ordinal_width, "little")
            result[self.ordinal_offset:self.ordinal_offset + self.ordinal_width] = encoded
            if self.mirror_offset is not None:
                result[self.mirror_offset:self.mirror_offset + self.ordinal_width] = encoded
        return bytes(result)


def pe_fixture(wide=False):
    data = bytearray(0x600)
    data[:2] = b"MZ"
    u16(data, 8, 4)
    u16(data, 0x18, 0x40)
    u32(data, 0x3c, 0x80)
    data[0x80:0x84] = b"PE\0\0"
    u16(data, 0x84, 0x8664 if wide else 0x14c)
    u16(data, 0x86, 1)
    optional = 0x98
    fixed, optional_size, width = (112, 240, 8) if wide else (96, 224, 4)
    u16(data, 0x94, optional_size)
    u16(data, 0x96, 0x102)
    u16(data, optional, 0x20b if wide else 0x10b)
    u32(data, optional + 16, 0x1000)
    if wide:
        struct.pack_into("<Q", data, optional + 24, 0x140000000)
    else:
        u32(data, optional + 28, 0x400000)
    u32(data, optional + 32, 0x1000)
    u32(data, optional + 36, 0x200)
    u32(data, optional + 56, 0x2000)
    u32(data, optional + 60, 0x200)
    u32(data, optional + fixed - 4, 16)
    u32(data, optional + fixed + 8, 0x1000)
    u32(data, optional + fixed + 12, 40)
    section = optional + optional_size
    data[section:section + 8] = b".idata\0\0"
    for offset, value in ((8, 0x400), (12, 0x1000), (16, 0x400),
                          (20, 0x200), (36, 0xc0000040)):
        u32(data, section + offset, value)
    u32(data, 0x200, 0x1080)
    u32(data, 0x20c, 0x1060)
    u32(data, 0x210, 0x10c0)
    data[0x260:0x26d] = b"KERNEL32.dll\0"
    flag = 1 << (width * 8 - 1)
    for offset in (0x280, 0x2c0):
        data[offset:offset + width] = (0x1100).to_bytes(width, "little")
        data[offset + width:offset + 2 * width] = (flag | 123).to_bytes(width, "little")
    data[0x302:0x30e] = b"ExitProcess\0"
    return ImportFixture(
        "PE32+" if wide else "PE32", bytes(data), b"KERNEL32.dll", b"OTHERLIB.dll",
        0x260, 0x200, b"ExitProcess", b"OtherSymbol", 0x302, 0x280, 1,
        2, 0x280 + width, width, 65535, flag, 0x2c0 + width)


def ne_fixture():
    data = bytearray(1024)
    data[:2] = b"MZ"
    u32(data, 0x3c, 0x80)
    data[0x80:0x82] = b"NE"
    for offset, value in ((4, 0x70), (6, 1), (28, 1), (30, 2),
                          (34, 0x40), (40, 0x48), (42, 0x50), (50, 4)):
        u16(data, 0x80 + offset, value)
    data[0x80 + 54] = 2
    for offset, value in ((0, 0x20), (2, 0x10), (4, 0x100), (6, 0x10)):
        u16(data, 0xc0 + offset, value)
    u16(data, 0xca, 7)
    data[0xd0:0xe6] = b"\x06KERNEL\x04USER\x09FatalExit"
    u16(data, 0x200, 0xffff)
    u16(data, 0x204, 0xffff)
    u16(data, 0x210, 2)
    data[0x212:0x222] = bytes((3, 2, 0, 0, 1, 0, 12, 0,
                              3, 1, 4, 0, 2, 0, 123, 0))
    return ImportFixture("NE", bytes(data), b"KERNEL", b"SYSTEM", 0xd1, 0xc8,
                         b"FatalExit", b"OtherExit", 0xdd, 0x212, 2, 3,
                         0x220, 2, 65535)


def linear_fixture(lx=False):
    header = 0x80
    data = bytearray(header + 0x2200)
    data[:2] = b"MZ"
    u16(data, 2, len(data) % 512)
    u16(data, 4, (len(data) + 511) // 512)
    u16(data, 8, 4)
    u16(data, 0x18, 0x40)
    u32(data, 0x3c, header)
    data[header:header + 2] = b"LX" if lx else b"LE"
    u16(data, header + 8, 2)
    u16(data, header + 10, 1)
    for offset, value in ((0x14, 2), (0x18, 1), (0x20, 1), (0x24, 8192),
                          (0x28, 4096), (0x2c, 0 if lx else 4096),
                          (0x38, 0x30), (0x40, 0xb0), (0x44, 1), (0x48, 0xc8),
                          (0x68, 0xe0), (0x6c, 0xec), (0x70, 0x160),
                          (0x74, 2), (0x78, 0x16f), (0x80, header + 0x200),
                          (0xb0, 8192), (0xb4, 0x1000), (0xb8, 0x2005),
                          (0xbc, 1), (0xc0, 2)):
        u32(data, header + offset, value)
    if lx:
        u16(data, header + 0xcc, 4096)
        u32(data, header + 0xd0, 4096)
        u16(data, header + 0xd4, 4096)
    else:
        data[header + 0xca] = 1
        data[header + 0xce] = 2
    records = header + 0xec
    first = bytes((7, 2, 0, 0, 1, 1, 0, 2, 0, 2, 0, 1))
    second = bytes((7, 0x81, 4, 0, 2, 7))
    data[records:records + len(first)] = first
    data[records + len(first):records + len(first) + len(second)] = second
    u32(data, header + 0xe4, len(first))
    u32(data, header + 0xe8, len(first) + len(second))
    modules, procedures = header + 0x160, header + 0x16f
    data[modules:procedures] = b"\x08DOSCALLS\x05OTHER"
    data[procedures:procedures + 18] = b"\0\x07DosOpen\x08DosClose"
    u32(data, header + 0x30, procedures + 18 - (header + 0xe0))
    data[header + 0x200] = 0xc3
    return ImportFixture("LX" if lx else "LE", bytes(data), b"DOSCALLS", b"NEWMODUL",
                         modules + 1, modules, b"DosOpen", b"DosRead", procedures + 2,
                         records, 2, 3, records + len(first) + 5, 1, 255)


def nlm_fixture():
    data = bytearray(512)
    data[:24] = b"NetWare Loadable Module\x1a"
    u32(data, 24, 4)
    data[28:37] = b"\x08TEST.NLM"
    for offset, value in ((42, 256), (46, 32), (50, 288), (54, 32), (58, 16),
                          (62, 432), (66, 4), (70, 320), (74, 1),
                          (78, 416), (82, 1), (86, 336), (90, 2),
                          (94, 384), (98, 1), (102, 400), (106, 1), (110, 4)):
        u32(data, offset, value)
    data[256:288] = b"\x90" * 31 + b"\xc3"
    data[320:329] = b"\x08CLIB.NLM"
    data[336:343] = b"\x06printf"
    u32(data, 343, 2)
    u32(data, 347, 0x40000004)
    u32(data, 351, 0x80000008)
    data[355:360] = b"\x04puts"
    data[384:391] = b"\x06start_"
    u32(data, 391, 0x80000000)
    data[400] = 1
    data[405:411] = b"\x05start"
    u32(data, 416, 0xc0000008)
    return ImportFixture("NLM", bytes(data), b"CLIB.NLM", b"TEST.NLM", 321, 320,
                         b"printf", b"rename", 337, 336, 1, None, None,
                         header_offset=0)


def all_fixtures():
    return [pe_fixture(), pe_fixture(True), ne_fixture(), linear_fixture(),
            linear_fixture(True), nlm_fixture()]


if __name__ == "__main__":
    import argparse
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("directory", type=Path)
    options = parser.parse_args()
    options.directory.mkdir(parents=True, exist_ok=True)
    for fixture in all_fixtures():
        filename = options.directory / (fixture.format.replace("+", "plus").lower() + ".bin")
        filename.write_bytes(fixture.data)
        print(filename)
