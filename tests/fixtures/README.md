# Binary fixtures

These small, deterministic files test opening mapped binary files and decoding
known x86 instructions. They are generated locally from the instruction bytes
below; no compiler, assembler, downloaded executable, or executable invocation is
required. Offsets are file offsets in hexadecimal; instructions use Intel syntax.

| File | Mode | Instructions (offset: instruction) |
| --- | --- | --- |
| `raw_x86_16.bin` | Real or compatibility 16-bit | `00: mov ax,0x1234`; `03: inc ax`; `04: add ax,1`; `07: ret` |
| `raw_x86_32.bin` | Compatibility 32-bit | `00: mov eax,0x12345678`; `05: add eax,1`; `08: xor ebx,ebx`; `0a: ret` |
| `raw_x86_64.bin` | Long 64-bit | `00: mov eax,60`; `05: mov edi,42`; `0a: syscall` |
| `minimal_exit_x86_64.elf` | Long 64-bit | `78: mov eax,60`; `7d: mov edi,42`; `82: syscall` |
| `invalid_truncated.bin` | Long 64-bit | `00: db 06` (invalid opcode); `01: nop`; `02: db E8` (incomplete call) |
| `empty.bin` | Any | Zero bytes; no instructions |

The ELF is a 132-byte, little-endian ELF64 executable for x86-64 Linux. Its
64-byte ELF header points to one 56-byte `PT_LOAD` program header at `0x40`.
That read/execute segment maps the complete file at `0x400000`; the entry point
is `0x400078`. It has no section table, interpreter, or external dependencies.
Its payload implements the Linux `exit(42)` syscall. Tests inspect and disassemble
the file without executing it. LHiew displays file offsets, not virtual addresses.

Regenerate or verify the committed files from the repository root:

```sh
python3 tests/fixtures/generate.py
python3 tests/fixtures/generate.py --check
ctest --test-dir build -R '^binary_fixtures$' --output-on-failure
```

The C expectations are maintained independently from the generator. They check
instruction lengths, register/immediate operands, EOF clearing, short viewports,
and decoding after opening each file through `open_file_to_view()`.

Run fixture checks in both Debug and optimized Release builds;
[development.md](../../docs/development.md#build-and-test) also gives the sanitizer
configuration. The shared endian readers never cast serialized headers to C
structures, so unaligned data remains valid on all supported build hosts.
