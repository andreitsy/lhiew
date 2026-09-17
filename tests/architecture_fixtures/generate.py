"""Generate architecture fixtures without cross-compilers; --check verifies bytes."""

import argparse
from pathlib import Path
import struct


def words(values, width, big_endian=False):
    return b"".join(value.to_bytes(width, "big" if big_endian else "little")
                    for value in values)


X86_16 = bytes.fromhex("b8 34 12 83 c0 01 c3")
X86_32 = bytes.fromhex("b8 78 56 34 12 83 c0 01 c3")
X86_64 = bytes.fromhex("48 b8 78 56 34 12 00 00 00 00 48 83 c0 01 c3")
ARM = [0xE3A00001, 0xE2800002, 0xE12FFF1E]  # mov; add; bx lr
THUMB = [0x2001, 0x3002, 0x4770]  # movs; adds; bx lr
AARCH64 = words([0xD2800020, 0x91000800, 0xD65F03C0], 4)
MIPS32 = [0x24020001, 0x24420002, 0x03E00008, 0]
MIPS64 = [0x64020001, 0x64420002, 0x03E00008, 0]
PPC32 = [0x38600001, 0x38630002, 0x4E800020]
PPC64 = [0xE8640000, 0x38630002, 0x4E800020]
SPARC = words([0x90102001, 0x90022002, 0x81C3E008, 0x01000000], 4, True)
SPARCV9 = bytes.fromhex("81 a8 0a 24 89 a0 10 20 89 a0 1a 60")
SYSTEMZ = bytes.fromhex("a7 09 00 01 a7 0b 00 02 07 fe")
M68K = bytes.fromhex("70 01 52 80 4e 75")
RISCV32 = bytes.fromhex("13 05 10 00 09 05 67 80 00 00")
RISCV64 = bytes.fromhex("1b 05 15 00 09 05 67 80 00 00")
SH = [0xE001, 0x7002, 0x000B, 0x0009]
TRICORE = bytes.fromhex("82 00 c2 00 00 90")
XCORE = bytes.fromhex("fe 0f fe 17 13 17")
# Capstone's published C64x example, serialized as little-endian words.
TMS320C64X = words([0x01AC8840, 0x81AC8843, 0x00000000], 4)
M6809 = bytes.fromhex("86 01 8b 02 39")  # lda #1; adda #2; rts
MOS6502 = bytes.fromhex("a9 01 69 02 60")  # lda #1; adc #2; rts


def elf(machine, code, bits=32, big_endian=False, thumb=False, flags=0):
    endian = ">" if big_endian else "<"
    ident = b"\x7fELF" + bytes([2 if bits == 64 else 1,
                                2 if big_endian else 1, 1]) + bytes(9)
    entry = 0x10100 | int(thumb)
    size = 0x100 + len(code)
    if bits == 64:
        header = struct.pack(endian + "16sHHIQQQIHHHHHH", ident, 2, machine, 1,
                             entry, 64, 0, flags, 64, 56, 1, 0, 0, 0)
        segment = struct.pack(endian + "IIQQQQQQ", 1, 5, 0, 0x10000, 0x10000,
                              size, size, 0x1000)
    else:
        header = struct.pack(endian + "16sHHIIIIIHHHHHH", ident, 2, machine, 1,
                             entry, 52, 0, flags, 52, 32, 1, 0, 0, 0)
        segment = struct.pack(endian + "IIIIIIII", 1, 0, 0x10000, 0x10000,
                              size, size, 5, 0x1000)
    return (header + segment).ljust(0x100, b"\0") + code


def pe(machine, code, bits=32):
    optional_size = 112 if bits == 64 else 96
    result = bytearray(0x200 + len(code))
    result[:2] = b"MZ"
    struct.pack_into("<I", result, 0x3C, 0x80)
    result[0x80:0x84] = b"PE\0\0"
    struct.pack_into("<HHIIIHH", result, 0x84, machine, 1, 0, 0, 0,
                     optional_size, 0x22)
    optional = 0x98
    struct.pack_into("<H", result, optional, 0x20B if bits == 64 else 0x10B)
    struct.pack_into("<I", result, optional + 16, 0x1000)
    struct.pack_into("<I", result, optional + 20, 0x1000)
    if bits == 64:
        struct.pack_into("<Q", result, optional + 24, 0x140000000)
    else:
        struct.pack_into("<I", result, optional + 28, 0x400000)
    struct.pack_into("<II", result, optional + 32, 0x1000, 0x200)
    struct.pack_into("<II", result, optional + 56, 0x2000, 0x200)
    section = optional + optional_size
    struct.pack_into("<8sIIIIIIHHI", result, section, b".text", len(code),
                     0x1000, len(code), 0x200, 0, 0, 0, 0, 0x60000020)
    result[0x200:] = code
    return bytes(result)


def te(machine, code):
    # PI TE image: section file offsets retain the removed PE header's size.
    code_offset = 0x60
    stripped_size = 0x120
    result = bytearray(code_offset + len(code))
    struct.pack_into("<2sHBBHIIQ", result, 0, b"VZ", machine, 1, 10,
                     stripped_size, 0x1000, 0x1000, 0x80000000)
    struct.pack_into("<8sIIIIIIHHI", result, 40, b".text", len(code),
                     0x1000, len(code), code_offset + stripped_size - 40,
                     0, 0, 0, 0, 0x60000020)
    result[code_offset:] = code
    return bytes(result)


def macho(cpu, code, bits=32, big_endian=False):
    endian = ">" if big_endian else "<"
    size = 0x100 + len(code)
    if bits == 64:
        segment = struct.pack(endian + "II16sQQQQIIII", 0x19, 72, b"__TEXT",
                              0x10000, size, 0, size, 7, 5, 0, 0)
        header = struct.pack(endian + "IIIIIIII", 0xFEEDFACF, cpu, 0, 2,
                             2, 96, 0, 0)
    else:
        segment = struct.pack(endian + "II16sIIIIIIII", 1, 56, b"__TEXT",
                              0x10000, size, 0, size, 7, 5, 0, 0)
        header = struct.pack(endian + "IIIIIII", 0xFEEDFACE, cpu, 0, 2,
                             2, 80, 0)
    main = struct.pack(endian + "IIQQ", 0x80000028, 24, 0x100, 0)
    return (header + segment + main).ljust(0x100, b"\0") + code


def mz(code):
    result = bytearray(0x40 + len(code))
    result[:2] = b"MZ"
    # One partial 512-byte page, four header paragraphs, CS:IP = 0:0.
    struct.pack_into("<13H", result, 2, len(result), 1, 0, 4, 0, 0xFFFF,
                     0, 0x100, 0, 0, 0, 0x1C, 0)
    result[0x40:] = code
    return bytes(result)


def fixtures():
    result = {
        "elf_x86_32.bin": elf(3, X86_32),
        "elf_x86_64.bin": elf(62, X86_64, 64),
        "elf_x86_x32.bin": elf(62, X86_64),
        "elf_aarch64.bin": elf(183, AARCH64, 64),
        "elf_aarch64_be_container.bin": elf(183, AARCH64, 64, True),
        "elf_sparc32.bin": elf(2, SPARC, big_endian=True),
        "elf_sparc64.bin": elf(43, SPARCV9, 64, True),
        "elf_systemz.bin": elf(22, SYSTEMZ, 64, True),
        "elf_m68k.bin": elf(4, M68K, big_endian=True),
        "elf_riscv32.bin": elf(243, RISCV32, flags=1),
        "elf_riscv64.bin": elf(243, RISCV64, 64, flags=1),
        "elf_tricore.bin": elf(44, TRICORE),
        "elf_xcore.bin": elf(203, XCORE),
        "elf_tms320c64x.bin": elf(140, TMS320C64X),
        "elf_arm_be8.bin": elf(40, words(ARM, 4), big_endian=True,
                                flags=0x00800000),
        "raw_x86_16.bin": X86_16,
        "raw_m6809.bin": M6809,
        "raw_mos6502.bin": MOS6502,
        "raw_arm_truncated.bin": words(ARM[:1], 4) + b"\xff\xff",
        "raw_aarch64_invalid.bin": bytes.fromhex("ff ff ff ff") + AARCH64,
        "mz_x86_16.bin": mz(X86_16),
        "pe_x86_32.bin": pe(0x14C, X86_32),
        "pe_x86_64.bin": pe(0x8664, X86_64, 64),
        "pe_thumb.bin": pe(0x1C4, words(THUMB, 2)),
        "pe_aarch64.bin": pe(0xAA64, AARCH64, 64),
        "te_x86_64.bin": te(0x8664, X86_64),
        "te_aarch64.bin": te(0xAA64, AARCH64),
        "macho_x86_32.bin": macho(7, X86_32),
        "macho_x86_64.bin": macho(0x01000007, X86_64, 64),
        "macho_arm.bin": macho(12, words(ARM, 4)),
        "macho_aarch64.bin": macho(0x0100000C, AARCH64, 64),
        "macho_ppc32.bin": macho(18, words(PPC32, 4, True), big_endian=True),
        "macho_ppc64.bin": macho(0x01000012, words(PPC64, 4, True), 64, True),
        "elf_unknown.bin": elf(0xFFFE, b"\x90\xc3"),
        "elf_truncated.bin": b"\x7fELF\x02\x01\x01",
        "pe_truncated.bin": pe(0x8664, X86_64, 64)[:0x90],
        "macho_truncated.bin": macho(7, X86_32)[:20],
        "elf_arm_branch.bin": elf(40, words([0xEA000000, *ARM], 4)),
        "elf_x86_64_branch.bin": elf(62, bytes.fromhex("e8 00 00 00 00") + X86_64, 64),
        "elf_aarch64_branch.bin": elf(183, words([0x14000002], 4) + AARCH64, 64),
        "elf_riscv64_branch.bin": elf(243, words([0x008000EF], 4) + RISCV64, 64, flags=1),
        "elf_thumb_it.bin": elf(40, bytes.fromhex("0c bf 01 20 02 20 70 47"),
                                 thumb=True),
    }
    for be, suffix in [(False, "le"), (True, "be")]:
        for name, machine, code, width, bits in [
            ("arm", 40, ARM, 4, 32), ("thumb", 40, THUMB, 2, 32),
            ("mips32", 8, MIPS32, 4, 32), ("mips64", 8, MIPS64, 4, 64),
            ("ppc32", 20, PPC32, 4, 32), ("ppc64", 21, PPC64, 4, 64),
            ("sh", 42, SH, 2, 32),
        ]:
            result[f"elf_{name}_{suffix}.bin"] = elf(
                machine, words(code, width, be), bits, be, name == "thumb")
        endian = ">" if be else "<"
        ebpf = b"".join(struct.pack(endian + "BBhi", opcode, 0, 0, immediate)
                        for opcode, immediate in [(0xB7, 1), (0x07, 2), (0x95, 0)])
        result[f"elf_ebpf_{suffix}.bin"] = elf(247, ebpf, 64, be)

    # Universal Mach-O has a 64-bit ARM slice before an x86-64 slice.
    arm_slice = result["macho_aarch64.bin"]
    x86_slice = result["macho_x86_64.bin"]
    fat_header = struct.pack(">II", 0xCAFEBABE, 2)
    fat_header += struct.pack(">IIIII", 0x0100000C, 0, 0x1000, len(arm_slice), 12)
    fat_header += struct.pack(">IIIII", 0x01000007, 0, 0x2000, len(x86_slice), 12)
    result["macho_fat.bin"] = (fat_header.ljust(0x1000, b"\0") + arm_slice
                               ).ljust(0x2000, b"\0") + x86_slice
    return result


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
    print(f"{len(fixtures())} architecture fixtures "
          + ("verified." if args.check else "regenerated."))


if __name__ == "__main__":
    main()
