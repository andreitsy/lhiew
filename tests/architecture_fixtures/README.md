# Architecture fixtures

These small binary files exercise the normal file-opening path, header detection,
entry-point mapping, and disassembly together. They contain deterministic serialized
headers and hand-selected instructions; no cross-compiler, emulator, or host-specific
assembler is needed. All files use `.bin` so the repository's executable ignore rules
do not hide them. They are minimal parser fixtures, not programs intended to execute
on the host.

Regenerate or check the committed bytes from the repository root:

```sh
python3 tests/architecture_fixtures/generate.py
python3 tests/architecture_fixtures/generate.py --check
ctest --test-dir build -R 'architectures|architecture_fixture_integrity' --output-on-failure
```

`tests/test_architectures.c` opens each fixture with `open_file_to_view`, verifies
the detected CPU, instruction endianness, format, and file entry offset, and checks
the complete instruction sequence: instruction boundaries, mnemonic, and operands.
The same suite exercises manual profiles, restoring Auto detection, mapped branch
targets, malformed headers, invalid/truncated instructions, navigation, and resizing.

| Fixtures | Header/CPU and representative instructions |
| --- | --- |
| `elf_x86_32.bin`, `elf_x86_64.bin` | ELF i386/AMD64; move immediate to EAX/RAX, add, return |
| `elf_x86_x32.bin` | x32 ABI: 32-bit ELF container with 64-bit AMD64 instructions |
| `elf_arm_{le,be}.bin` | ELF ARM; move immediate, add, branch through LR |
| `elf_arm_be8.bin` | Big-endian ELF metadata with little-endian ARM instructions |
| `elf_thumb_{le,be}.bin` | ELF ARM with low entry bit set; Thumb move/add/branch |
| `elf_aarch64.bin` | ELF AArch64; 64-bit move/add/return |
| `elf_aarch64_be_container.bin` | Big-endian ELF AArch64 metadata with little-endian instruction bytes |
| `elf_mips{32,64}_{le,be}.bin` | ELF MIPS; ADDIU/DADDIU, register jump, delay-slot NOP |
| `elf_ppc{32,64}_{le,be}.bin` | ELF PowerPC; immediate/load, add, return through LR |
| `elf_sparc32.bin`, `elf_sparc64.bin` | Big-endian ELF SPARC/V9; integer/return and V9 floating-point conversions |
| `elf_systemz.bin` | Big-endian ELF s390x; LGHI, AGHI, branch register |
| `elf_m68k.bin` | Big-endian ELF Motorola 68000; MOVEQ, ADDQ.L, RTS |
| `elf_riscv{32,64}.bin` | ELF RISC-V; ADDI/ADDIW, compressed C.ADDI, return |
| `elf_ebpf_{le,be}.bin` | ELF eBPF; MOV64, ADD64, EXIT |
| `elf_sh_{le,be}.bin` | ELF SuperH; move/add immediates, RTS, delay-slot NOP |
| `elf_tricore.bin` | ELF TriCore; move, add, return |
| `elf_xcore.bin` | ELF XCore; GET, LDW, SETD |
| `elf_tms320c64x.bin` | ELF TI C6000; ADD on both register banks and NOP |
| `pe_x86_{32,64}.bin`, `pe_thumb.bin`, `pe_aarch64.bin` | PE32/PE32+ section and RVA mapping with the corresponding CPU sequences |
| `te_x86_64.bin`, `te_aarch64.bin` | UEFI TE AMD64/AArch64 with stripped-header section offset adjustment |
| `macho_{x86_32,x86_64,arm,aarch64,ppc32,ppc64}.bin` | Thin Mach-O, both byte orders, LC_SEGMENT/LC_SEGMENT_64 and LC_MAIN |
| `macho_fat.bin` | Universal Mach-O containing AArch64 then AMD64 slices |
| `mz_x86_16.bin` | DOS MZ with a real-mode 16-bit move/add/return sequence |
| `raw_x86_16.bin` | Headerless code, tested with real and protected 16-bit profiles |
| `raw_m6809.bin`, `raw_mos6502.bin` | Headerless legacy CPU code; load/add immediate and return |
| `elf_arm_branch.bin` | ARM branch whose target must use the mapped virtual address |
| `elf_{x86_64,aarch64,riscv64}_branch.bin` | PC-relative branches with mapped virtual PCs; RISC-V preserves the decoder's relative-displacement operand syntax |
| `elf_thumb_it.bin` | Thumb conditional IT block retains EQ/NE suffixes across centering, redraw, and resize |
| `raw_arm_truncated.bin`, `raw_aarch64_invalid.bin` | Byte fallback at truncated/invalid instructions |
| `elf_unknown.bin`, `elf_truncated.bin`, `pe_truncated.bin`, `macho_truncated.bin` | Unsupported or incomplete headers remain available for byte inspection and manual override |

ELF code starts at file offset `0x100`, mapped to virtual address `0x10100` by
one executable PT_LOAD segment. PE code starts at `0x200` and RVA `0x1000`.
TE code starts at `0x60`, adjusted from the original section file offset `0x158`,
and maps to virtual address `0x80001000`.
Thin Mach-O code starts at `0x100` and virtual address `0x10100`; the first
universal slice's code is at `0x1100`. Universal files are recognized as requiring
manual slice inspection; tests select each CPU profile and decode both slices.
The DOS load module starts at `0x40`.

The SPARC V9, XCore, and TMS320C64x sequences also appear in Capstone's
`tests/test_sparc.c`, `tests/test_xcore.c`, and `tests/test_tms320c64x.c` examples.
TriCore encodings are covered by its `suite/MC/TriCore/tc162.s.cs` instruction
vectors. Expected strings are fixed test data, independently checked against
those encodings and the pinned Capstone 5.0.9 disassembler; the tests never
generate their expectations using the decoder under test.
