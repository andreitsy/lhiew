# Architecture support: analysis and implementation

## Scope and terminology

An instruction set (ISA), a decoder mode, assembly syntax, and an executable
container are separate things. x86, ARM and RISC-V are ISAs; Thumb, word width
and byte order select encodings. Intel and AT&T are two x86 text syntaxes.
  ELF, PE and Mach-O carry CPU metadata and map file bytes to runtime addresses.
An assembler encodes text into bytes; this feature adds **disassembly**, not an
assembler or file editing.

There is no header capable of identifying every possible instruction stream.
Raw firmware and DOS COM files need a manual profile. One executable can also
contain code for several CPUs, instruction modes, or a virtual machine.

## Engine analysis

- **Zydis** already supplies x86 real/protected 16-bit, 32-bit and 64-bit decoding,
  including the existing AVX support and AT&T formatting. Keep it to preserve
  the established x86 behavior.
- **Capstone 5.0.9** is the pinned stable non-x86 backend. Its C API fits the C17
  core, supports the native families below, and permits instruction-at-a-time
  decoding without launching another process. Version 6 adds more targets but
  is still an alpha series. See [upstream releases](https://www.capstone-engine.org/changelog.html),
  [architecture and mode definitions](https://github.com/capstone-engine/capstone/blob/5.0.9/include/capstone/capstone.h),
  and [iteration API](https://www.capstone-engine.org/iteration.html).
- **LLVM** also exposes a [C disassembler API](https://llvm.org/doxygen/group__LLVMCDisassembler.html),
  but introduces a substantially larger dependency. External objdump processes
  would complicate interactive paging, error handling and precise byte offsets.

Capstone is a Git submodule, with only the selected native backends enabled.
Its duplicate x86, EVM and WebAssembly decoders, tools and upstream test programs
are excluded from the application build. The engine's BSD license is retained
in its submodule; Zydis retains its own license.

## Implemented profiles

There are 32 manual profiles plus **Auto (file header)**. LE/BE below describes
instruction bytes, which can differ from the container's byte order.

| CPU family | Manual profiles | Automatic header recognition |
|---|---|---|
| x86 | real16, protected16, 32, 64 | MZ real16; ELF, PE/TE, Mach-O 32/64 |
| ARM | ARM and Thumb/Thumb2, each LE/BE | ELF, PE/TE; ARM Mach-O |
| AArch64 | A64, LE instructions | ELF, PE/TE, Mach-O |
| MIPS | 32/64, each LE/BE | ELF; selected legacy PE machine IDs |
| PowerPC | 32/64, each LE/BE | ELF, Mach-O; legacy PE32 |
| SPARC | 32 and V9, BE | ELF; legacy Mach-O32 |
| IBM SystemZ | BE | ELF |
| Motorola 68K | 68040 decoder, BE | ELF, legacy Mach-O |
| RISC-V | RV32/RV64 with compressed instructions | ELF, PE/TE |
| eBPF | LE/BE | ELF |
| SuperH | SH4 with FPU, LE/BE | ELF, legacy PE |
| TriCore | 1.6.2, LE | ELF |
| XCore | LE | ELF |
| TI TMS320C64x | LE | ELF |
| Motorola 6809 | 6809 | Raw bytes, manual |
| MOS 6502 | 6502 | Raw bytes, manual |

These are selected decoder modes, not a claim to implement every extension or
CPU generation in each family. Header identifiers such as EM_68K or EM_SH do
not fully describe CPU variants; the displayed profile makes that choice visible.

## Implementation sequence and architecture

1. Add a bounded, host-independent header parser (`binary.c`). Read integers
   byte by byte, validate table spans before accessing entries, handle arithmetic
   overflow, and distinguish raw, detected, unsupported and malformed files.
2. Describe profiles separately from file containers (`architecture.c`). Detect
   when loading a file; allow manual override and a return to Auto. Keep
   `cur_byte` as a file offset. Resolve a mapped instruction's runtime address
   from its section or segment without changing navigation coordinates.
3. Route instruction decoding to Zydis or Capstone (`disassembler.c`). Share
   centered rows, cached boundaries, invalid-byte fallback and paging. Respect
   instruction alignment and mapped region limits. Decode each lookback/viewport
   in order so Thumb IT state does not leak from a second pass.
4. Add the architecture menu (`Shift-F1`, with `a` as a terminal-friendly fallback).
   Arrow keys/jk and Page Up/Down select; Enter applies; Escape cancels. Preserve
   the selected byte when changing CPU, and invalidate cached instruction rows.
   `e` opens the detected entry or first executable region in assembly view.
   Entering assembly from offset zero also uses that location.
5. Validate serialized files through the ordinary file reader, then decode their
   code bytes. Keep reproducible generators and expected instruction sequences
   in the repository; no cross compiler is needed to run tests.
6. Re-audit `FEATURES.md` against the resulting code and regression coverage.

## Container details

- **ELF32/64:** either container endian, program and section tables, extended
  table counts, and relocatable objects. Machine identifiers select the ISA;
  class alone does not (x86 x32 and AArch64 ILP32 still contain 64-bit ISA code).
  ARM entry bit zero selects Thumb; BE8 selects LE instruction bytes. AArch64
  instructions remain LE with a BE container. MIPS ISA flags and RISC-V
  RV64ILP32 are considered. See the [ELF header specification](https://gabi.xinuos.com/elf/02-eheader.html),
  [machine registry](https://gabi.xinuos.com/elf/a-emachine.html),
  [Arm ELF ABI](https://github.com/ARM-software/abi-aa/blob/main/aaelf32/aaelf32.rst),
  and [RISC-V ABI](https://github.com/riscv-non-isa/riscv-elf-psabi-doc/blob/master/riscv-elf.adoc).
- **PE32/PE32+:** validate DOS/PE signatures, COFF machine, optional header and
  sections. Convert entry RVA to file offset; branch text uses image base plus
  RVA. ARMNT selects Thumb. **TE** handles the stripped-header offset adjustment.
  See [Microsoft PE format](https://learn.microsoft.com/en-us/windows/win32/debug/pe-format)
  and [UEFI PI TE definition](https://uefi.org/specs/PI/1.8A/V1_TE_Image.html).
- **Thin Mach-O32/64:** use CPU type, byte order, sections/segments and LC_MAIN.
  Map LC_MAIN's offset from the preferred __TEXT address through executable
  segments, including images whose __TEXT file offset is nonzero.
  Without LC_MAIN, use the first executable region; this is not a full
  LC_UNIXTHREAD entry decoder. See [Apple loader definitions](https://github.com/apple-oss-distributions/xnu/blob/main/EXTERNAL_HEADERS/mach-o/loader.h).
- **DOS MZ:** use real16, header paragraphs and CS:IP. First check for PE, NE,
  LE or LX signatures so a newer executable is not mistaken for DOS code.

When an entry is absent or cannot be mapped, the first file-backed executable
region is used where available. This is a viewing starting point, not execution
or relocation. Relocatable objects have unresolved relocations; branch text
uses their stored section addresses. Data can still be viewed as instructions
by moving into it; this is not a code/data analysis engine.

All decoders receive the mapped runtime PC. Their native assembly conventions
are retained: x86/ARM/AArch64 show absolute branch destinations, while Capstone's
RISC-V JAL text uses the relative displacement (for example, `jal 8`). That
displacement is not a file offset. Runtime addresses are not a separate gutter
column or a virtual-address navigation feature.

## Explicit limits and follow-up work

- **Universal Mach-O:** detect and validate the slice table, report unsupported;
  a slice chooser and per-slice address/mode state remain future work.
- **Mixed modes:** ARM/Thumb mapping symbols and interworking, MIPS16/microMIPS/R6,
  and PE ARM64EC/ARM64X need further handling. Unsupported MIPS flags and PE
  hybrid machine IDs are reported as unsupported. Choose ARM/Thumb manually
  for a mixed-mode location in otherwise supported files.
- **Other ISAs:** Alpha, PA-RISC, Xtensa, ARC, LoongArch, AVR, MSP430, VAX,
  8051, Z80 and many DSP/GPU sets need additional backends/profiles and tests.
  No promise of universal instruction coverage is made.
- **Virtual machines:** JVM bytecode, .NET IL, WebAssembly and EVM need their own
  container/method handling; their presence cannot be inferred from native
  machine type alone. Classic BPF is not the eBPF profile.
- **Legacy containers:** NE/LE/LX are recognized as unsupported; COFF objects,
  archives, firmware containers, compressed/packed code and encrypted images
  need separate parsing or extraction.
- **Navigation and analysis:** no header browser, symbols/import/export browser,
  VA goto, relocation application, branch following or user-forced re-sync yet.
  Backward decoding of variable-length instructions remains heuristic when a
  known boundary lies outside the bounded lookback.

## Regression evidence

`tests/architecture_fixtures/generate.py` writes deterministic minimal files and
`--check` verifies the checked-in bytes. `test_architectures.c` loads those files
through `open_file_to_view`, verifies detected profiles and mapped entry offsets,
and compares instruction lengths and assembly. Coverage includes every exposed
manual profile, both supported byte orders, compressed RISC-V, mapped branch
addresses, override/Auto, invalid/truncated data, resize, centering and paging.
`test_binary.c` covers parser truncation, corrupt tables, unsupported CPUs and
formats, arithmetic boundaries and container-specific details. PTY tests verify
real Shift-F1 sequences, menu navigation/cancellation, entry navigation and resize.
