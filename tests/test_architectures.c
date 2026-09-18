#include "lhiew/types.h"
#include "test_harness.h"
#include "lhiew/architecture.h"
#include "lhiew/disassembler.h"
#include "lhiew/editor.h"
#include "lhiew/file_buffer.h"
#include "lhiew/input.h"
#include "lhiew/terminal.h"

#include <sys/mman.h>

#ifndef LHIEW_ARCHITECTURE_FIXTURE_DIR
#error "LHIEW_ARCHITECTURE_FIXTURE_DIR must name the architecture fixture directory"
#endif

typedef struct expectedInstruction {
    size_t size;
    const char *text;
} expectedInstruction;

typedef struct fixtureCase {
    const char *filename;
    architectureId architecture;
    int big_endian;
    disassemblerMode x86_mode;
    binaryFormat format;
    size_t entry;
    const expectedInstruction *instructions;
    size_t instruction_count;
} fixtureCase;

static const expectedInstruction x86_16[] = {
    {3, "mov $0x1234, %ax"}, {3, "add $0x01, %ax"}, {1, "ret"},
};
static const expectedInstruction x86_32[] = {
    {5, "mov $0x12345678, %eax"}, {3, "add $0x01, %eax"}, {1, "ret"},
};
static const expectedInstruction x86_64[] = {
    {10, "mov $0x12345678, %rax"}, {4, "add $0x01, %rax"}, {1, "ret"},
};
static const expectedInstruction arm[] = {
    {4, "mov r0, #1"}, {4, "add r0, r0, #2"}, {4, "bx lr"},
};
static const expectedInstruction thumb[] = {
    {2, "movs r0, #1"}, {2, "adds r0, #2"}, {2, "bx lr"},
};
static const expectedInstruction aarch64[] = {
    {4, "mov x0, #1"}, {4, "add x0, x0, #2"}, {4, "ret"},
};
static const expectedInstruction mips32[] = {
    {4, "addiu $v0, $zero, 1"}, {4, "addiu $v0, $v0, 2"},
    {4, "jr $ra"}, {4, "nop"},
};
static const expectedInstruction mips64[] = {
    {4, "daddiu $v0, $zero, 1"}, {4, "daddiu $v0, $v0, 2"},
    {4, "jr $ra"}, {4, "nop"},
};
static const expectedInstruction ppc32[] = {
    {4, "li r3, 1"}, {4, "addi r3, r3, 2"}, {4, "blr"},
};
static const expectedInstruction ppc64[] = {
    {4, "ld r3, 0(r4)"}, {4, "addi r3, r3, 2"}, {4, "blr"},
};
static const expectedInstruction sparc32[] = {
    {4, "mov 1, %o0"}, {4, "add %o0, 2, %o0"}, {4, "retl"}, {4, "nop"},
};
static const expectedInstruction sparc64[] = {
    {4, "fcmps %f0, %f4"}, {4, "fstox %f0, %f4"}, {4, "fqtoi %f0, %f4"},
};
static const expectedInstruction systemz[] = {
    {4, "lghi %r0, 1"}, {4, "aghi %r0, 2"}, {2, "br %r14"},
};
static const expectedInstruction m68k[] = {
    {2, "moveq #$1, d0"}, {2, "addq.l #$1, d0"}, {2, "rts"},
};
static const expectedInstruction riscv32[] = {
    {4, "addi a0, zero, 1"}, {2, "c.addi a0, 2"}, {4, "ret"},
};
static const expectedInstruction riscv64[] = {
    {4, "addiw a0, a0, 1"}, {2, "c.addi a0, 2"}, {4, "ret"},
};
static const expectedInstruction ebpf[] = {
    {8, "mov64 r0, 0x1"}, {8, "add64 r0, 0x2"}, {8, "exit"},
};
static const expectedInstruction sh[] = {
    {2, "mov #1,r0"}, {2, "add #2,r0"}, {2, "rts"}, {2, "nop"},
};
static const expectedInstruction tricore[] = {
    {2, "mov d0, #0"}, {2, "add d0, #0"}, {2, "ret"},
};
static const expectedInstruction xcore[] = {
    {2, "get r11, ed"}, {2, "ldw et, sp[4]"}, {2, "setd res[r3], r4"},
};
static const expectedInstruction tms320c64x[] = {
    {4, "add a11, a4, a3"}, {4, "add b11, b4, b3"}, {4, "NOP"},
};
static const expectedInstruction m6809[] = {
    {2, "lda #1"}, {2, "adda #2"}, {1, "rts"},
};
static const expectedInstruction mos6502[] = {
    {2, "lda #0x01"}, {2, "adc #0x02"}, {1, "rts"},
};
static const expectedInstruction x86_64_branch[] = {
    {5, "call 0x0000000000010105"}, {10, "mov $0x12345678, %rax"},
    {4, "add $0x01, %rax"}, {1, "ret"},
};
static const expectedInstruction aarch64_branch[] = {
    {4, "b #0x10108"}, {4, "mov x0, #1"}, {4, "add x0, x0, #2"}, {4, "ret"},
};
static const expectedInstruction riscv64_branch[] = {
    /* Capstone's RISC-V syntax prints the PC-relative displacement. */
    {4, "jal 8"}, {4, "addiw a0, a0, 1"}, {2, "c.addi a0, 2"}, {4, "ret"},
};

#define CASE(file, arch, endian, mode, kind, offset, sequence) \
    {file ".bin", arch, endian, mode, kind, offset, sequence, \
     sizeof(sequence) / sizeof(sequence[0])}
#define ELF(file, arch, endian, sequence) \
    CASE(file, arch, endian, MODE_LONG_COMPAT_32, BINARY_FORMAT_ELF, 0x100, sequence)

static const fixtureCase header_cases[] = {
    ELF("elf_x86_32", ARCH_X86, 0, x86_32),
    CASE("elf_x86_64", ARCH_X86, 0, MODE_LONG_COMPAT_64, BINARY_FORMAT_ELF, 0x100, x86_64),
    CASE("elf_x86_x32", ARCH_X86, 0, MODE_LONG_COMPAT_64, BINARY_FORMAT_ELF, 0x100, x86_64),
    CASE("elf_x86_64_branch", ARCH_X86, 0, MODE_LONG_COMPAT_64, BINARY_FORMAT_ELF, 0x100, x86_64_branch),
    ELF("elf_arm_le", ARCH_ARM, 0, arm), ELF("elf_arm_be", ARCH_ARM, 1, arm),
    ELF("elf_thumb_le", ARCH_THUMB, 0, thumb), ELF("elf_thumb_be", ARCH_THUMB, 1, thumb),
    ELF("elf_arm_be8", ARCH_ARM, 0, arm),
    ELF("elf_aarch64", ARCH_AARCH64, 0, aarch64),
    ELF("elf_aarch64_be_container", ARCH_AARCH64, 0, aarch64),
    ELF("elf_aarch64_branch", ARCH_AARCH64, 0, aarch64_branch),
    ELF("elf_mips32_le", ARCH_MIPS32, 0, mips32), ELF("elf_mips32_be", ARCH_MIPS32, 1, mips32),
    ELF("elf_mips64_le", ARCH_MIPS64, 0, mips64), ELF("elf_mips64_be", ARCH_MIPS64, 1, mips64),
    ELF("elf_ppc32_le", ARCH_PPC32, 0, ppc32), ELF("elf_ppc32_be", ARCH_PPC32, 1, ppc32),
    ELF("elf_ppc64_le", ARCH_PPC64, 0, ppc64), ELF("elf_ppc64_be", ARCH_PPC64, 1, ppc64),
    ELF("elf_sparc32", ARCH_SPARC32, 1, sparc32), ELF("elf_sparc64", ARCH_SPARC64, 1, sparc64),
    ELF("elf_systemz", ARCH_SYSTEMZ, 1, systemz), ELF("elf_m68k", ARCH_M68K, 1, m68k),
    ELF("elf_riscv32", ARCH_RISCV32, 0, riscv32), ELF("elf_riscv64", ARCH_RISCV64, 0, riscv64),
    ELF("elf_riscv64_branch", ARCH_RISCV64, 0, riscv64_branch),
    ELF("elf_ebpf_le", ARCH_EBPF, 0, ebpf), ELF("elf_ebpf_be", ARCH_EBPF, 1, ebpf),
    ELF("elf_sh_le", ARCH_SH, 0, sh), ELF("elf_sh_be", ARCH_SH, 1, sh),
    ELF("elf_tricore", ARCH_TRICORE, 0, tricore), ELF("elf_xcore", ARCH_XCORE, 0, xcore),
    ELF("elf_tms320c64x", ARCH_TMS320C64X, 0, tms320c64x),
    CASE("mz_x86_16", ARCH_X86, 0, REAL, BINARY_FORMAT_MZ, 0x40, x86_16),
    CASE("pe_x86_32", ARCH_X86, 0, MODE_LONG_COMPAT_32, BINARY_FORMAT_PE, 0x200, x86_32),
    CASE("pe_x86_64", ARCH_X86, 0, MODE_LONG_COMPAT_64, BINARY_FORMAT_PE, 0x200, x86_64),
    CASE("pe_thumb", ARCH_THUMB, 0, MODE_LONG_COMPAT_32, BINARY_FORMAT_PE, 0x200, thumb),
    CASE("pe_aarch64", ARCH_AARCH64, 0, MODE_LONG_COMPAT_32, BINARY_FORMAT_PE, 0x200, aarch64),
    CASE("te_x86_64", ARCH_X86, 0, MODE_LONG_COMPAT_64, BINARY_FORMAT_TE, 0x60, x86_64),
    CASE("te_aarch64", ARCH_AARCH64, 0, MODE_LONG_COMPAT_32, BINARY_FORMAT_TE, 0x60, aarch64),
    CASE("macho_x86_32", ARCH_X86, 0, MODE_LONG_COMPAT_32, BINARY_FORMAT_MACHO, 0x100, x86_32),
    CASE("macho_x86_64", ARCH_X86, 0, MODE_LONG_COMPAT_64, BINARY_FORMAT_MACHO, 0x100, x86_64),
    CASE("macho_arm", ARCH_ARM, 0, MODE_LONG_COMPAT_32, BINARY_FORMAT_MACHO, 0x100, arm),
    CASE("macho_aarch64", ARCH_AARCH64, 0, MODE_LONG_COMPAT_32, BINARY_FORMAT_MACHO, 0x100, aarch64),
    CASE("macho_ppc32", ARCH_PPC32, 1, MODE_LONG_COMPAT_32, BINARY_FORMAT_MACHO, 0x100, ppc32),
    CASE("macho_ppc64", ARCH_PPC64, 1, MODE_LONG_COMPAT_32, BINARY_FORMAT_MACHO, 0x100, ppc64),
};

static const fixtureCase raw_cases[] = {
    CASE("raw_x86_16", ARCH_X86, 0, REAL, BINARY_FORMAT_RAW, 0, x86_16),
    CASE("raw_x86_16", ARCH_X86, 0, MODE_LONG_COMPAT_16, BINARY_FORMAT_RAW, 0, x86_16),
    CASE("raw_m6809", ARCH_M680X, 1, MODE_LONG_COMPAT_32, BINARY_FORMAT_RAW, 0, m6809),
    CASE("raw_mos6502", ARCH_MOS65XX, 0, MODE_LONG_COMPAT_32, BINARY_FORMAT_RAW, 0, mos6502),
};

static void cleanup_fixture(void) {
    if (global_cfg.file && global_cfg.file != MAP_FAILED)
        munmap(global_cfg.file, global_cfg.num_bytes);
    if (global_cfg.fp)
        fclose(global_cfg.fp);
    free(global_cfg.filename);
    free_disassembler_buffer();
    RESET_GLOBAL_CFG();
}

static void open_fixture(const char *name) {
    cleanup_fixture();
    global_cfg.mode = DISASSEMBLER_MODE;
    global_cfg.disassembler_mode = MODE_LONG_COMPAT_32;
    editor_resize(26, 100);
    char path[1024];
    snprintf(path, sizeof(path), "%s/%s", LHIEW_ARCHITECTURE_FIXTURE_DIR, name);
    open_file_to_view(path);
    switch_mode();
}

static size_t find_profile(architectureId id, int big_endian, disassemblerMode mode) {
    for (size_t i = 1; i < architecture_profile_count(); ++i) {
        const architectureSpec *spec = &architecture_profile(i)->spec;
        if (spec->id == id && spec->big_endian == big_endian &&
            (id != ARCH_X86 || spec->x86_mode == mode))
            return i;
    }
    return 0;
}

static const disassemblerRow *find_row(size_t offset) {
    for (size_t i = 0; i < global_cfg.screenrows; ++i) {
        const disassemblerRow *row = &global_cfg.disassembler_buffer[i];
        if (row->start_byte == offset && row->end_byte > offset)
            return row;
    }
    return NULL;
}

static void assert_sequence(size_t offset, const expectedInstruction *expected, size_t count) {
    for (size_t i = 0; i < count; ++i) {
        const disassemblerRow *row = find_row(offset);
        if (!row)
            fprintf(stderr, "  Missing instruction at 0x%zx in %s\n", offset, global_cfg.filename);
        ASSERT_NE(row, NULL);
        ASSERT_EQ(row->end_byte, offset + expected[i].size);
        ASSERT_STR_EQ(row->diss_str, expected[i].text);
        offset += expected[i].size;
    }
}

static void verify_header_case(const fixtureCase *test) {
    open_fixture(test->filename);
    const binaryInfo *info = architecture_binary_info();
    ASSERT_NE(info, NULL);
    ASSERT_EQ(info->status, BINARY_DETECTED);
    ASSERT_EQ(info->format, test->format);
    ASSERT_EQ(global_cfg.architecture, test->architecture);
    ASSERT_EQ(global_cfg.big_endian, test->big_endian);
    ASSERT_EQ(global_cfg.architecture_manual, 0);
    if (test->architecture == ARCH_X86)
        ASSERT_EQ(global_cfg.disassembler_mode, test->x86_mode);
    ASSERT(info->has_entry);
    ASSERT_EQ(info->entry_offset, test->entry);
    architecture_jump_to_entry();
    ASSERT_EQ(global_cfg.cur_byte, test->entry);
    ASSERT_EQ(disassemble_block(global_cfg.cur_byte), EXIT_SUCCESS);
    assert_sequence(test->entry, test->instructions, test->instruction_count);
}

static void test_manual_profiles_for_every_decoder(void) {
    for (size_t profile = 1; profile < architecture_profile_count(); ++profile) {
        const architectureSpec *spec = &architecture_profile(profile)->spec;
        const fixtureCase *test = NULL;
        for (size_t i = 0; i < sizeof(header_cases) / sizeof(header_cases[0]); ++i) {
            const fixtureCase *candidate = &header_cases[i];
            if (candidate->architecture == spec->id &&
                candidate->big_endian == spec->big_endian &&
                (spec->id != ARCH_X86 || candidate->x86_mode == spec->x86_mode)) {
                test = candidate;
                break;
            }
        }
        for (size_t i = 0; !test && i < sizeof(raw_cases) / sizeof(raw_cases[0]); ++i) {
            const fixtureCase *candidate = &raw_cases[i];
            if (candidate->architecture == spec->id &&
                candidate->big_endian == spec->big_endian &&
                (spec->id != ARCH_X86 || candidate->x86_mode == spec->x86_mode))
                test = candidate;
        }
        ASSERT_NE(test, NULL);
        open_fixture(test->filename);
        architecture_select(profile);
        ASSERT_EQ(global_cfg.architecture_manual, 1);
        ASSERT_EQ(architecture_current_profile(), profile);
        ASSERT_EQ(disassemble_block(test->entry), EXIT_SUCCESS);
        assert_sequence(test->entry, test->instructions, test->instruction_count);
    }
}

static void test_manual_legacy_raw_files(void) {
    for (size_t i = 0; i < sizeof(raw_cases) / sizeof(raw_cases[0]); ++i) {
        const fixtureCase *test = &raw_cases[i];
        open_fixture(test->filename);
        ASSERT_EQ(architecture_binary_info()->status, BINARY_RAW);
        size_t profile = find_profile(test->architecture, test->big_endian, test->x86_mode);
        ASSERT_NE(profile, (size_t)0);
        architecture_select(profile);
        ASSERT_EQ(global_cfg.architecture, test->architecture);
        ASSERT_EQ(disassemble_block(0), EXIT_SUCCESS);
        assert_sequence(0, test->instructions, test->instruction_count);
    }
}

static void test_universal_macho_requires_manual_slice_inspection(void) {
    open_fixture("macho_fat.bin");
    const binaryInfo *info = architecture_binary_info();
    ASSERT_EQ(info->format, BINARY_FORMAT_MACHO_FAT);
    ASSERT_EQ(info->status, BINARY_UNSUPPORTED);
    ASSERT_EQ(global_cfg.architecture, ARCH_UNKNOWN);
    ASSERT_EQ(info->has_entry, 0);
    architecture_select(find_profile(ARCH_AARCH64, 0, MODE_LONG_COMPAT_32));
    ASSERT_EQ(disassemble_block(0x1100), EXIT_SUCCESS);
    assert_sequence(0x1100, aarch64, sizeof(aarch64) / sizeof(aarch64[0]));
    architecture_select(find_profile(ARCH_X86, 0, MODE_LONG_COMPAT_64));
    ASSERT_EQ(disassemble_block(0x2100), EXIT_SUCCESS);
    assert_sequence(0x2100, x86_64, sizeof(x86_64) / sizeof(x86_64[0]));
}

static void test_override_and_auto_restore_header(void) {
    open_fixture("elf_arm_le.bin");
    global_cfg.cur_byte = 0x104;
    architecture_select(find_profile(ARCH_X86, 0, MODE_LONG_COMPAT_64));
    ASSERT_EQ(global_cfg.architecture, ARCH_X86);
    ASSERT_EQ(global_cfg.architecture_manual, 1);
    ASSERT_EQ(global_cfg.cur_byte, (size_t)0x104);
    architecture_select(0);
    ASSERT_EQ(global_cfg.architecture, ARCH_ARM);
    ASSERT_EQ(global_cfg.architecture_manual, 0);
    ASSERT_EQ(global_cfg.cur_byte, (size_t)0x104);
    ASSERT_EQ(disassemble_block(global_cfg.cur_byte), EXIT_SUCCESS);
    assert_sequence(0x100, arm, sizeof(arm) / sizeof(arm[0]));
    architecture_select(find_profile(ARCH_THUMB, 0, MODE_LONG_COMPAT_32));
    architecture_detect_file();
    ASSERT_EQ(global_cfg.architecture, ARCH_ARM);
    ASSERT_EQ(global_cfg.architecture_manual, 0);

    open_fixture("raw_m6809.bin");
    architecture_select(find_profile(ARCH_M680X, 1, MODE_LONG_COMPAT_32));
    architecture_select(0);
    ASSERT_EQ(global_cfg.architecture, ARCH_X86);
    ASSERT_EQ(global_cfg.disassembler_mode, MODE_LONG_COMPAT_32);
    ASSERT_EQ(global_cfg.architecture_manual, 0);
}

static void test_mapped_branch_uses_virtual_address(void) {
    open_fixture("elf_arm_branch.bin");
    architecture_jump_to_entry();
    ASSERT_EQ(disassemble_block(global_cfg.cur_byte), EXIT_SUCCESS);
    const expectedInstruction branch[] = {{4, "b #0x10108"}};
    assert_sequence(0x100, branch, 1);
    assert_sequence(0x104, arm, sizeof(arm) / sizeof(arm[0]));

    /* RISC-V keeps its native relative operand, while the container still maps
       the branch's file position to the correct virtual PC. */
    open_fixture("elf_riscv64_branch.bin");
    architecture_jump_to_entry();
    binaryRegion region;
    ASSERT(architecture_region(global_cfg.cur_byte, &region));
    uint64_t pc = region.address + global_cfg.cur_byte - region.offset;
    ASSERT_EQ(pc, UINT64_C(0x10100));
    ASSERT_EQ(disassemble_block(global_cfg.cur_byte), EXIT_SUCCESS);
    assert_sequence(0x100, riscv64_branch, sizeof(riscv64_branch) / sizeof(riscv64_branch[0]));
}

static void test_truncated_and_invalid_instructions(void) {
    open_fixture("raw_arm_truncated.bin");
    architecture_select(find_profile(ARCH_ARM, 0, MODE_LONG_COMPAT_32));
    ASSERT_EQ(disassemble_block(0), EXIT_SUCCESS);
    const expectedInstruction truncated[] = {
        {4, "mov r0, #1"}, {1, "db FF"}, {1, "db FF"},
    };
    assert_sequence(0, truncated, 3);

    open_fixture("raw_aarch64_invalid.bin");
    architecture_select(find_profile(ARCH_AARCH64, 0, MODE_LONG_COMPAT_32));
    ASSERT_EQ(disassemble_block(0), EXIT_SUCCESS);
    const disassemblerRow *row = find_row(0);
    ASSERT_NE(row, NULL);
    ASSERT_STR_EQ(row->diss_str, "db FF");
    ASSERT_GT(row->end_byte, row->start_byte);
    ASSERT_EQ(disassemble_block(global_cfg.num_bytes), EXIT_SUCCESS);
    for (size_t i = 0; i < global_cfg.screenrows; ++i)
        ASSERT_STR_EQ(global_cfg.disassembler_buffer[i].diss_str, "");
}

static void test_unknown_and_malformed_headers_remain_inspectable(void) {
    const char *names[] = {"elf_unknown.bin", "elf_truncated.bin",
                           "pe_truncated.bin", "macho_truncated.bin"};
    for (size_t i = 0; i < sizeof(names) / sizeof(names[0]); ++i) {
        open_fixture(names[i]);
        ASSERT_EQ(architecture_binary_info()->status,
                  i ? BINARY_MALFORMED : BINARY_UNSUPPORTED);
        ASSERT_EQ(global_cfg.architecture, ARCH_UNKNOWN);
        ASSERT_EQ(disassemble_block(0), EXIT_SUCCESS);
        const disassemblerRow *row = find_row(0);
        ASSERT_NE(row, NULL);
        ASSERT_EQ(row->end_byte, (size_t)1);
        ASSERT(strncmp(row->diss_str, "db ", 3) == 0);
        architecture_select(find_profile(ARCH_X86, 0, MODE_LONG_COMPAT_32));
        ASSERT_EQ(global_cfg.architecture, ARCH_X86);
        ASSERT_EQ(global_cfg.architecture_manual, 1);
    }
}

static void test_variable_width_navigation_and_resize(void) {
    open_fixture("elf_riscv64.bin");
    architecture_jump_to_entry();
    ASSERT_EQ(disassemble_block(global_cfg.cur_byte), EXIT_SUCCESS);
    editor_move_cursor(ARROW_DOWN);
    ASSERT_EQ(global_cfg.cur_byte, (size_t)0x104);
    editor_move_cursor(ARROW_DOWN);
    ASSERT_EQ(global_cfg.cur_byte, (size_t)0x106);
    editor_move_cursor(ARROW_UP);
    /* Up selects the last byte in the preceding instruction. */
    ASSERT_EQ(global_cfg.cur_byte, (size_t)0x105);
    editor_resize(10, 60);
    ASSERT_EQ(global_cfg.cur_byte, (size_t)0x105);
    ASSERT_EQ(disassemble_block(global_cfg.cur_byte), EXIT_SUCCESS);
    assert_sequence(0x100, riscv64, sizeof(riscv64) / sizeof(riscv64[0]));
    editor_move_cursor(PAGE_DOWN);
    ASSERT_EQ(global_cfg.cur_byte, global_cfg.num_bytes);
    editor_move_cursor(PAGE_UP);
    ASSERT(global_cfg.cur_byte < global_cfg.num_bytes);
}

static void test_thumb_it_context_survives_centering_and_redraw(void) {
    open_fixture("elf_thumb_it.bin");
    const expectedInstruction conditional[] = {
        {2, "ite eq"}, {2, "moveq r0, #1"}, {2, "movne r0, #2"}, {2, "bx lr"},
    };
    architecture_jump_to_entry();
    ASSERT_EQ(global_cfg.architecture, ARCH_THUMB);
    ASSERT_EQ(disassemble_block(global_cfg.cur_byte), EXIT_SUCCESS);
    assert_sequence(0x100, conditional, 4);

    /* The IT instruction scrolls above this three-row viewport. Conditions must
       still be retained when history is decoded and each frame is rebuilt. */
    global_cfg.cur_byte = 0x104;
    editor_resize(5, 60);
    for (size_t redraw = 0; redraw < 3; ++redraw) {
        ASSERT_EQ(disassemble_block(global_cfg.cur_byte), EXIT_SUCCESS);
        assert_sequence(0x102, conditional + 1, 3);
    }
    editor_resize(26, 100);
    ASSERT_EQ(disassemble_block(global_cfg.cur_byte), EXIT_SUCCESS);
    assert_sequence(0x100, conditional, 4);
}

static void test_profile_bounds_and_auto_label(void) {
    open_fixture("elf_aarch64.bin");
    ASSERT_EQ(architecture_current_profile(), (size_t)0);
    ASSERT_STR_EQ(architecture_current_name(), "AArch64");
    ASSERT_EQ(architecture_profile(architecture_profile_count()), NULL);
    ASSERT_EQ(architecture_profile(SIZE_MAX), NULL);

    global_cfg.cur_byte = 0x104;
    architecture_select(SIZE_MAX);
    ASSERT_EQ(global_cfg.cur_byte, (size_t)0x104);
    ASSERT_EQ(global_cfg.architecture, ARCH_AARCH64);
    ASSERT_EQ(architecture_current_profile(), (size_t)0);

    for (size_t index = 1; index < architecture_profile_count(); ++index) {
        architecture_select(index);
        ASSERT_EQ(architecture_current_profile(), index);
        ASSERT_STR_EQ(architecture_current_name(), architecture_profile(index)->name);
        ASSERT_EQ(global_cfg.cur_byte, (size_t)0x104);
    }
}

static void test_detection_cache_requires_same_mapping_and_size(void) {
    open_fixture("elf_aarch64.bin");
    ASSERT_NE(architecture_binary_info(), NULL);
    size_t size = global_cfg.num_bytes;
    global_cfg.num_bytes--;
    ASSERT_EQ(architecture_binary_info(), NULL);
    binaryRegion region;
    ASSERT_EQ(architecture_region(0x100, &region), 0);
    global_cfg.num_bytes = size;

    uint8_t *mapping = global_cfg.file;
    uint8_t different_file[] = {0x90};
    global_cfg.file = different_file;
    ASSERT_EQ(architecture_binary_info(), NULL);
    architecture_jump_to_entry();
    ASSERT_EQ(global_cfg.cur_byte, (size_t)0);
    global_cfg.file = mapping;
    ASSERT_NE(architecture_binary_info(), NULL);
    global_cfg.binary_detected = 0;
    ASSERT_EQ(architecture_binary_info(), NULL);
}

int main(void) {
    printf("test_architectures:\n");
    for (size_t i = 0; i < sizeof(header_cases) / sizeof(header_cases[0]); ++i) {
        int failures = _tests_failed;
        _tests_run++;
        verify_header_case(&header_cases[i]);
        if (_tests_failed == failures) {
            _tests_passed++;
            printf("  PASS %s\n", header_cases[i].filename);
        } else {
            fprintf(stderr, "  Fixture: %s\n", header_cases[i].filename);
        }
    }
    RUN_TEST(test_manual_profiles_for_every_decoder);
    RUN_TEST(test_manual_legacy_raw_files);
    RUN_TEST(test_universal_macho_requires_manual_slice_inspection);
    RUN_TEST(test_override_and_auto_restore_header);
    RUN_TEST(test_mapped_branch_uses_virtual_address);
    RUN_TEST(test_truncated_and_invalid_instructions);
    RUN_TEST(test_unknown_and_malformed_headers_remain_inspectable);
    RUN_TEST(test_variable_width_navigation_and_resize);
    RUN_TEST(test_thumb_it_context_survives_centering_and_redraw);
    RUN_TEST(test_profile_bounds_and_auto_label);
    RUN_TEST(test_detection_cache_requires_same_mapping_and_size);
    cleanup_fixture();
    TEST_REPORT();
}
