#include "lhiew/types.h"
#include "lhiew/binary.h"
#include "test_harness.h"

static void put16(uint8_t *p, uint16_t value, int be) {
    p[be ? 1 : 0] = (uint8_t)value;
    p[be ? 0 : 1] = (uint8_t)(value >> 8);
}

static void put32(uint8_t *p, uint32_t value, int be) {
    for (size_t i = 0; i < 4; ++i) p[be ? 3 - i : i] = (uint8_t)(value >> (8 * i));
}

static void put64(uint8_t *p, uint64_t value, int be) {
    for (size_t i = 0; i < 8; ++i) p[be ? 7 - i : i] = (uint8_t)(value >> (8 * i));
}

/* Small serialized headers, independent of platform ELF/PE/Mach-O structs. */
static void make_elf(uint8_t data[512], int wide, int be, uint16_t machine) {
    memset(data, 0, 512);
    memcpy(data, "\x7f" "ELF", 4);
    data[4] = wide ? 2 : 1;
    data[5] = be ? 2 : 1;
    data[6] = 1;
    put16(data + 16, 2, be);
    put16(data + 18, machine, be);
    put32(data + 20, 1, be);
    size_t phoff = wide ? 64 : 52;
    put16(data + (wide ? 52 : 40), (uint16_t)phoff, be);
    put16(data + (wide ? 54 : 42), wide ? 56 : 32, be);
    put16(data + (wide ? 56 : 44), 1, be);
    put32(data + phoff, 1, be);
    if (wide) {
        put64(data + 24, 0x401004, be);
        put64(data + 32, phoff, be);
        put32(data + phoff + 4, 5, be);
        put64(data + phoff + 8, 256, be);
        put64(data + phoff + 16, 0x401000, be);
        put64(data + phoff + 32, 16, be);
        put64(data + phoff + 40, 16, be);
    } else {
        put32(data + 24, 0x401004, be);
        put32(data + 28, (uint32_t)phoff, be);
        put32(data + phoff + 4, 256, be);
        put32(data + phoff + 8, 0x401000, be);
        put32(data + phoff + 16, 16, be);
        put32(data + phoff + 20, 16, be);
        put32(data + phoff + 24, 5, be);
    }
}

static void test_elf_architectures_and_addresses(void) {
    const struct {
        uint16_t machine;
        int wide;
        architectureId id;
    } cases[] = {
        {2, 0, ARCH_SPARC32}, {18, 0, ARCH_SPARC32}, {43, 1, ARCH_SPARC64},
        {3, 0, ARCH_X86}, {62, 1, ARCH_X86}, {4, 0, ARCH_M68K},
        {8, 0, ARCH_MIPS32}, {8, 1, ARCH_MIPS64}, {10, 0, ARCH_MIPS32},
        {20, 0, ARCH_PPC32}, {21, 1, ARCH_PPC64}, {22, 1, ARCH_SYSTEMZ},
        {40, 0, ARCH_ARM}, {42, 0, ARCH_SH}, {44, 0, ARCH_TRICORE},
        {140, 0, ARCH_TMS320C64X}, {183, 1, ARCH_AARCH64}, {203, 0, ARCH_XCORE},
        {243, 0, ARCH_RISCV32}, {243, 1, ARCH_RISCV64}, {247, 1, ARCH_EBPF},
    };
    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i) {
        for (int be = 0; be <= 1; ++be) {
            uint8_t data[512];
            binaryInfo info;
            binaryRegion region;
            make_elf(data, cases[i].wide, be, cases[i].machine);
            binary_detect(data, sizeof(data), &info);
            int unsupported = (cases[i].id == ARCH_SPARC32 || cases[i].id == ARCH_SPARC64 ||
                               cases[i].id == ARCH_SYSTEMZ || cases[i].id == ARCH_M68K) ? !be :
                              (cases[i].id == ARCH_RISCV32 || cases[i].id == ARCH_RISCV64 ||
                               cases[i].id == ARCH_TRICORE || cases[i].id == ARCH_XCORE ||
                               cases[i].id == ARCH_TMS320C64X) ? be : 0;
            ASSERT_EQ(info.status, unsupported ? BINARY_UNSUPPORTED : BINARY_DETECTED);
            ASSERT_EQ(info.architecture.id, unsupported ? ARCH_UNKNOWN : cases[i].id);
            ASSERT_EQ(info.architecture.big_endian,
                      cases[i].id == ARCH_X86 || cases[i].id == ARCH_AARCH64 ? 0 : be);
            ASSERT_EQ(info.entry_offset, (size_t)260);
            ASSERT(info.has_entry);
            ASSERT(binary_region_at(data, sizeof(data), &info, 260, &region));
            ASSERT_EQ(region.offset, (size_t)256);
            ASSERT_EQ(region.size, (size_t)16);
            ASSERT_EQ(region.address, UINT64_C(0x401000));
            ASSERT(region.executable);
            ASSERT(!binary_region_at(data, sizeof(data), &info, 272, &region));
            ASSERT(!binary_region_at(data, sizeof(data), &info, 0, &region));
        }
    }
}

static void test_elf_machine_modes(void) {
    uint8_t data[512];
    binaryInfo info;
    make_elf(data, 0, 1, 40);
    put32(data + 24, 0x401005, 1);
    put32(data + 36, 0x00800000, 1);
    binary_detect(data, sizeof(data), &info);
    ASSERT_EQ(info.architecture.id, ARCH_THUMB);
    ASSERT_EQ(info.architecture.big_endian, 0);
    ASSERT_EQ(info.entry_offset, (size_t)260);
    make_elf(data, 0, 0, 62); /* x32 ABI still uses 64-bit x86 instructions. */
    binary_detect(data, sizeof(data), &info);
    ASSERT_EQ(info.architecture.x86_mode, MODE_LONG_COMPAT_64);
    make_elf(data, 0, 1, 8);
    put32(data + 36, 0x60000000, 1); /* MIPS64 ISA, ELF32 ABI. */
    binary_detect(data, sizeof(data), &info);
    ASSERT_EQ(info.architecture.id, ARCH_MIPS64);
    put32(data + 36, 0x02000000, 1); /* microMIPS is not ordinary MIPS32. */
    binary_detect(data, sizeof(data), &info);
    ASSERT_EQ(info.status, BINARY_UNSUPPORTED);
    ASSERT_EQ(info.architecture.id, ARCH_UNKNOWN);
    make_elf(data, 0, 0, 243);
    put32(data + 36, 0x20, 0); /* RV64 ILP32. */
    binary_detect(data, sizeof(data), &info);
    ASSERT_EQ(info.architecture.id, ARCH_RISCV64);
    make_elf(data, 1, 0, 258); /* LoongArch needs a different decoder. */
    binary_detect(data, sizeof(data), &info);
    ASSERT_EQ(info.status, BINARY_UNSUPPORTED);
    ASSERT_EQ(info.architecture.id, ARCH_UNKNOWN);
}

static void test_elf_relocatable_sections_and_extended_counts(void) {
    uint8_t data[512];
    binaryInfo info;
    binaryRegion region;
    for (int wide = 0; wide <= 1; ++wide) {
        for (int be = 0; be <= 1; ++be) {
            make_elf(data, wide, be, wide ? 21 : 20);
            put16(data + 16, 1, be);
            put16(data + (wide ? 56 : 44), 0, be);
            put16(data + (wide ? 58 : 46), wide ? 64 : 40, be);
            put16(data + (wide ? 60 : 48), 0, be); /* Extended section count. */
            size_t shoff = 128;
            size_t section = shoff + (wide ? 64 : 40);
            if (wide) {
                put64(data + 24, 0, be);
                put64(data + 32, 0, be);
                put64(data + 40, shoff, be);
                put64(data + shoff + 32, 2, be);
                put32(data + section + 4, 1, be);
                put64(data + section + 8, 6, be);
                put64(data + section + 24, 256, be);
                put64(data + section + 32, 16, be);
            } else {
                put32(data + 24, 0, be);
                put32(data + 28, 0, be);
                put32(data + 32, (uint32_t)shoff, be);
                put32(data + shoff + 20, 2, be);
                put32(data + section + 4, 1, be);
                put32(data + section + 8, 6, be);
                put32(data + section + 16, 256, be);
                put32(data + section + 20, 16, be);
            }
            binary_detect(data, sizeof(data), &info);
            ASSERT_EQ(info.status, BINARY_DETECTED);
            ASSERT_EQ(info.entry_offset, (size_t)256);
            ASSERT(binary_region_at(data, sizeof(data), &info, 264, &region));
            ASSERT_EQ(region.address, UINT64_C(0));
            ASSERT_EQ(region.offset, (size_t)256);
            /* SHT_NOBITS must not expose bytes even if sh_offset points inside. */
            put32(data + section + 4, 8, be);
            binary_detect(data, sizeof(data), &info);
            ASSERT_EQ(info.status, BINARY_DETECTED);
            ASSERT(!info.has_entry);
            ASSERT(!binary_region_at(data, sizeof(data), &info, 264, &region));
        }
    }
}

static void test_elf_rejects_overflow_and_invalid_extents(void) {
    uint8_t data[512];
    binaryInfo info;
    binaryRegion region;
    make_elf(data, 1, 0, 62);
    put64(data + 32, UINT64_MAX - 8, 0);
    binary_detect(data, sizeof(data), &info);
    ASSERT_EQ(info.status, BINARY_MALFORMED);
    ASSERT(!binary_region_at(data, sizeof(data), &info, 256, &region));
    make_elf(data, 1, 0, 62);
    put64(data + 64 + 32, UINT64_MAX, 0);
    binary_detect(data, sizeof(data), &info);
    ASSERT_EQ(info.status, BINARY_MALFORMED);
    make_elf(data, 1, 0, 62);
    put64(data + 64 + 16, UINT64_MAX - 4, 0);
    binary_detect(data, sizeof(data), &info);
    ASSERT_EQ(info.status, BINARY_MALFORMED);
    make_elf(data, 1, 0, 62);
    put64(data + 64 + 40, 4, 0);
    binary_detect(data, sizeof(data), &info);
    ASSERT_EQ(info.status, BINARY_MALFORMED);
    make_elf(data, 1, 0, 62);
    put16(data + 54, 1, 0);
    binary_detect(data, sizeof(data), &info);
    ASSERT_EQ(info.status, BINARY_MALFORMED);
    make_elf(data, 1, 0, 62);
    put16(data + 56, 0xffff, 0);
    binary_detect(data, sizeof(data), &info);
    ASSERT_EQ(info.status, BINARY_MALFORMED);
}

static size_t make_pe(uint8_t data[1024], int wide, uint16_t machine) {
    memset(data, 0, 1024);
    memcpy(data, "MZ", 2);
    put32(data + 60, 64, 0);
    memcpy(data + 64, "PE\0\0", 4);
    put16(data + 68, machine, 0);
    put16(data + 70, 1, 0);
    size_t optional = 88;
    size_t optional_size = wide ? 112 : 96;
    put16(data + 84, (uint16_t)optional_size, 0);
    put16(data + optional, wide ? 0x20b : 0x10b, 0);
    put32(data + optional + 16, 0x1004, 0);
    if (wide) put64(data + optional + 24, 0x140000000, 0);
    else put32(data + optional + 28, 0x400000, 0);
    size_t section = optional + optional_size;
    memcpy(data + section, ".text", 5);
    put32(data + section + 8, 16, 0);
    put32(data + section + 12, 0x1000, 0);
    put32(data + section + 16, 512, 0);
    put32(data + section + 20, 512, 0);
    put32(data + section + 36, 0x60000020, 0);
    return section;
}

static void test_pe_sections_modes_and_bounds(void) {
    uint8_t data[1024];
    binaryInfo info;
    binaryRegion region;
    const struct { uint16_t machine; architectureId id; } cases[] = {
        {0x14c, ARCH_X86}, {0x8664, ARCH_X86}, {0x1c0, ARCH_ARM},
        {0x1c2, ARCH_THUMB}, {0x1c4, ARCH_THUMB}, {0xaa64, ARCH_AARCH64},
        {0x166, ARCH_MIPS32}, {0x1f0, ARCH_PPC32}, {0x5032, ARCH_RISCV32},
        {0x5064, ARCH_RISCV64}, {0x1a6, ARCH_SH},
    };
    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i) {
        int wide = cases[i].machine == 0x8664 || cases[i].machine == 0xaa64;
        make_pe(data, wide, cases[i].machine);
        binary_detect(data, sizeof(data), &info);
        ASSERT_EQ(info.status, BINARY_DETECTED);
        ASSERT_EQ(info.architecture.id, cases[i].id);
        ASSERT_EQ(info.entry_offset, (size_t)516);
        ASSERT(binary_region_at(data, sizeof(data), &info, 516, &region));
        ASSERT_EQ(region.address, wide ? UINT64_C(0x140001000) : UINT64_C(0x401000));
        ASSERT_EQ(region.size, (size_t)16); /* Raw alignment padding is not code. */
        ASSERT(!binary_region_at(data, sizeof(data), &info, 528, &region));
    }
    size_t section = make_pe(data, 1, 0xa641); /* ARM64EC hybrid. */
    binary_detect(data, sizeof(data), &info);
    ASSERT_EQ(info.status, BINARY_UNSUPPORTED);
    put32(data + section + 20, UINT32_MAX, 0);
    binary_detect(data, sizeof(data), &info);
    ASSERT_EQ(info.status, BINARY_MALFORMED);
    make_pe(data, 1, 0x8664);
    put64(data + 88 + 24, UINT64_MAX - 10, 0);
    binary_detect(data, sizeof(data), &info);
    ASSERT_EQ(info.status, BINARY_MALFORMED);
    make_pe(data, 1, 0x8664);
    put32(data + 88 + 108, 16, 0); /* Directory table exceeds optional header. */
    binary_detect(data, sizeof(data), &info);
    ASSERT_EQ(info.status, BINARY_MALFORMED);
}

static void test_te_header_adjustment(void) {
    uint8_t data[160] = {0};
    binaryInfo info;
    binaryRegion region;
    memcpy(data, "VZ", 2);
    put16(data + 2, 0xaa64, 0);
    data[4] = 1;
    put16(data + 6, 0x120, 0);
    put32(data + 8, 0x1004, 0);
    put64(data + 16, 0x80000000, 0);
    put32(data + 40 + 8, 16, 0);
    put32(data + 40 + 12, 0x1000, 0);
    put32(data + 40 + 16, 16, 0);
    put32(data + 40 + 20, 0x158, 0); /* 0x158 - (0x120 - 40) = 96. */
    put32(data + 40 + 36, 0x20000000, 0);
    binary_detect(data, sizeof(data), &info);
    ASSERT_EQ(info.status, BINARY_DETECTED);
    ASSERT_EQ(info.format, BINARY_FORMAT_TE);
    ASSERT_EQ(info.architecture.id, ARCH_AARCH64);
    ASSERT_EQ(info.entry_offset, (size_t)100);
    ASSERT(binary_region_at(data, sizeof(data), &info, 100, &region));
    ASSERT_EQ(region.offset, (size_t)96);
    ASSERT_EQ(region.address, UINT64_C(0x80001000));
    put32(data + 40 + 20, 1, 0);
    binary_detect(data, sizeof(data), &info);
    ASSERT_EQ(info.status, BINARY_MALFORMED);
}

static void test_mz_and_extended_dos_formats(void) {
    uint8_t data[128] = {0};
    binaryInfo info;
    binaryRegion region;
    memcpy(data, "MZ", 2);
    put16(data + 2, sizeof(data), 0);
    put16(data + 4, 1, 0);
    put16(data + 8, 4, 0);
    put16(data + 20, 3, 0);
    put16(data + 22, 1, 0);
    binary_detect(data, sizeof(data), &info);
    ASSERT_EQ(info.status, BINARY_DETECTED);
    ASSERT_EQ(info.architecture.x86_mode, REAL);
    ASSERT_EQ(info.entry_offset, (size_t)83);
    ASSERT(binary_region_at(data, sizeof(data), &info, 83, &region));
    ASSERT_EQ(region.address, UINT64_C(0));
    ASSERT_EQ(region.offset, (size_t)64);
    put32(data + 60, 96, 0);
    const char *signatures[] = {"NE", "LE", "LX"};
    for (size_t i = 0; i < 3; ++i) {
        memcpy(data + 96, signatures[i], 2);
        binary_detect(data, sizeof(data), &info);
        ASSERT_EQ(info.status, BINARY_UNSUPPORTED);
        ASSERT_EQ(info.architecture.id, ARCH_UNKNOWN);
        ASSERT(!info.has_entry);
    }
    put32(data + 60, UINT32_MAX, 0);
    binary_detect(data, sizeof(data), &info);
    ASSERT_EQ(info.status, BINARY_MALFORMED);
    put32(data + 60, 0, 0);
    put16(data + 6, 100, 0);
    put16(data + 24, 28, 0);
    binary_detect(data, sizeof(data), &info);
    ASSERT_EQ(info.status, BINARY_MALFORMED);
}

static void make_macho(uint8_t data[512], int wide, int be, uint32_t cpu) {
    memset(data, 0, 512);
    put32(data, wide ? 0xfeedfacf : 0xfeedface, be);
    put32(data + 4, cpu, be);
    put32(data + 12, 2, be);
    put32(data + 16, 2, be);
    size_t header = wide ? 32 : 28;
    size_t segment_size = wide ? 152 : 124;
    put32(data + 20, (uint32_t)(segment_size + 24), be);
    uint8_t *p = data + header;
    put32(p, wide ? 0x19 : 1, be);
    put32(p + 4, (uint32_t)segment_size, be);
    memcpy(p + 8, "__TEXT", 6);
    size_t fixed = wide ? 72 : 56;
    if (wide) {
        put64(p + 24, 0x100000000, be);
        put64(p + 32, 512, be);
        put64(p + 48, 512, be);
        put32(p + 60, 5, be);
        put32(p + 64, 1, be);
        put64(p + fixed + 32, 0x100000100, be);
        put64(p + fixed + 40, 16, be);
        put32(p + fixed + 48, 256, be);
        put32(p + fixed + 64, 0x80000400, be);
    } else {
        put32(p + 24, 0x10000, be);
        put32(p + 28, 512, be);
        put32(p + 36, 512, be);
        put32(p + 44, 5, be);
        put32(p + 48, 1, be);
        put32(p + fixed + 32, 0x10100, be);
        put32(p + fixed + 36, 16, be);
        put32(p + fixed + 40, 256, be);
        put32(p + fixed + 56, 0x80000400, be);
    }
    p += segment_size;
    put32(p, 0x80000028, be);
    put32(p + 4, 24, be);
    put64(p + 8, 260, be);
}

static void test_macho_endian_sections_and_commands(void) {
    uint8_t data[512];
    binaryInfo info;
    binaryRegion region;
    for (int wide = 0; wide <= 1; ++wide) {
        for (int be = 0; be <= 1; ++be) {
            make_macho(data, wide, be, wide ? 0x01000012 : 18);
            binary_detect(data, sizeof(data), &info);
            ASSERT_EQ(info.status, BINARY_DETECTED);
            ASSERT_EQ(info.architecture.id, wide ? ARCH_PPC64 : ARCH_PPC32);
            ASSERT_EQ(info.architecture.big_endian, be);
            ASSERT_EQ(info.entry_offset, (size_t)260);
            ASSERT(binary_region_at(data, sizeof(data), &info, 260, &region));
            ASSERT_EQ(region.offset, (size_t)256);
            ASSERT_EQ(region.size, (size_t)16);
            ASSERT_EQ(region.address, wide ? UINT64_C(0x100000100) : UINT64_C(0x10100));
        }
    }
    make_macho(data, 1, 0, 0x0100000c);
    put32(data + 32 + 4, UINT32_MAX, 0);
    binary_detect(data, sizeof(data), &info);
    ASSERT_EQ(info.status, BINARY_MALFORMED);
    make_macho(data, 1, 0, 0x0100000c);
    put32(data + 32 + 64, UINT32_MAX, 0);
    binary_detect(data, sizeof(data), &info);
    ASSERT_EQ(info.status, BINARY_MALFORMED);
    make_macho(data, 1, 0, 0x0100000c);
    put64(data + 32 + 72 + 32, UINT64_MAX - 1, 0);
    binary_detect(data, sizeof(data), &info);
    ASSERT_EQ(info.status, BINARY_MALFORMED);
}

static void test_macho_main_uses_segment_address_and_permissions(void) {
    uint8_t data[512];
    binaryInfo info;
    binaryRegion region;
    make_macho(data, 1, 0, 0x0100000c);
    put32(data + 32 + 72 + 64, 0, 0); /* No instruction attributes on section. */
    binary_detect(data, sizeof(data), &info);
    ASSERT_EQ(info.status, BINARY_DETECTED);
    ASSERT_EQ(info.entry_offset, (size_t)260);
    ASSERT(binary_region_at(data, sizeof(data), &info, 260, &region));
    ASSERT(region.executable);

    put64(data + 32 + 40, 128, 0); /* Nonzero __TEXT file offset. */
    put64(data + 32 + 48, 384, 0);
    put64(data + 32 + 72 + 32, 0x100000080, 0);
    put64(data + 32 + 152 + 8, 132, 0); /* VM offset132 maps to file260. */
    binary_detect(data, sizeof(data), &info);
    ASSERT_EQ(info.status, BINARY_DETECTED);
    ASSERT_EQ(info.entry_offset, (size_t)260);
    ASSERT(binary_region_at(data, sizeof(data), &info, 260, &region));
    ASSERT_EQ(region.address, UINT64_C(0x100000080));
    ASSERT(region.executable);

    put32(data + 32 + 60, 1, 0); /* Readable, not executable. */
    binary_detect(data, sizeof(data), &info);
    ASSERT_EQ(info.status, BINARY_MALFORMED);
    ASSERT(!info.has_entry);
    put32(data + 32 + 60, 5, 0);
    put64(data + 32 + 152 + 8, UINT64_MAX, 0);
    binary_detect(data, sizeof(data), &info);
    ASSERT_EQ(info.status, BINARY_MALFORMED);
    ASSERT(!info.has_entry);
}

static void test_nested_sections_limit_segment_mappings(void) {
    uint8_t data[512];
    binaryInfo info;
    binaryRegion region;
    make_elf(data, 1, 0, 62);
    put64(data + 64 + 8, 0, 0);
    put64(data + 64 + 16, 0x400000, 0);
    put64(data + 64 + 32, sizeof(data), 0);
    put64(data + 64 + 40, sizeof(data), 0);
    put64(data + 24, 0x700004, 0);
    put64(data + 40, 128, 0);
    put16(data + 58, 64, 0);
    put16(data + 60, 2, 0);
    put32(data + 192 + 4, 1, 0);
    put64(data + 192 + 8, 6, 0);
    put64(data + 192 + 16, 0x700000, 0);
    put64(data + 192 + 24, 256, 0);
    put64(data + 192 + 32, 16, 0);
    binary_detect(data, sizeof(data), &info);
    ASSERT_EQ(info.status, BINARY_DETECTED);
    ASSERT_EQ(info.entry_offset, (size_t)260);
    ASSERT(binary_region_at(data, sizeof(data), &info, 255, &region));
    ASSERT_EQ(region.offset, (size_t)0);
    ASSERT_EQ(region.size, (size_t)256); /* Only one byte may be decoded here. */
    ASSERT_EQ(region.address, UINT64_C(0x400000));
    ASSERT(binary_region_at(data, sizeof(data), &info, 256, &region));
    ASSERT_EQ(region.offset, (size_t)256);
    ASSERT_EQ(region.size, (size_t)16);
    ASSERT_EQ(region.address, UINT64_C(0x700000));
    ASSERT(binary_region_at(data, sizeof(data), &info, 272, &region));
    ASSERT_EQ(region.offset, (size_t)272); /* Lookback cannot start in .text. */
    ASSERT_EQ(region.size, (size_t)240);
    ASSERT_EQ(region.address, UINT64_C(0x400110));

    make_macho(data, 1, 0, 0x0100000c);
    put64(data + 32 + 72 + 32, 0x700000, 0);
    binary_detect(data, sizeof(data), &info);
    ASSERT_EQ(info.status, BINARY_DETECTED);
    ASSERT(binary_region_at(data, sizeof(data), &info, 255, &region));
    ASSERT_EQ(region.offset, (size_t)0);
    ASSERT_EQ(region.size, (size_t)256);
    ASSERT(binary_region_at(data, sizeof(data), &info, 256, &region));
    ASSERT_EQ(region.address, UINT64_C(0x700000));
    ASSERT_EQ(region.size, (size_t)16);
    ASSERT(binary_region_at(data, sizeof(data), &info, 272, &region));
    ASSERT_EQ(region.offset, (size_t)272);
    ASSERT_EQ(region.size, (size_t)240);
    ASSERT_EQ(region.address, UINT64_C(0x100000110));
}

static void test_macho_fat_is_explicitly_unsupported(void) {
    uint8_t data[256];
    binaryInfo info;
    for (int wide = 0; wide <= 1; ++wide) {
        for (int be = 0; be <= 1; ++be) {
            memset(data, 0, sizeof(data));
            put32(data, wide ? 0xcafebabf : 0xcafebabe, be);
            put32(data + 4, 1, be);
            if (wide) {
                put64(data + 16, 128, be);
                put64(data + 24, 128, be);
                put32(data + 32, 7, be);
            } else {
                put32(data + 16, 128, be);
                put32(data + 20, 128, be);
                put32(data + 24, 7, be);
            }
            binary_detect(data, sizeof(data), &info);
            ASSERT_EQ(info.status, BINARY_UNSUPPORTED);
            ASSERT_EQ(info.format, BINARY_FORMAT_MACHO_FAT);
            ASSERT_EQ(info.architecture.id, ARCH_UNKNOWN);
            ASSERT(!info.has_entry);
            put32(data + (wide ? 32 : 24), 64, be);
            binary_detect(data, sizeof(data), &info);
            ASSERT_EQ(info.status, BINARY_MALFORMED);
        }
    }
}

static void test_truncated_headers_and_raw_bytes(void) {
    uint8_t data[1024];
    binaryInfo info;
    binaryRegion region;
    const size_t header_sizes[] = {64, 28, 32, 112};
    for (size_t format = 0; format < 4; ++format) {
        if (format == 0) make_elf(data, 1, 0, 62);
        else if (format == 1) make_macho(data, 0, 1, 18);
        else if (format == 2) make_macho(data, 1, 0, 0x0100000c);
        else make_pe(data, 1, 0x8664);
        for (size_t size = 4; size < header_sizes[format]; ++size) {
            binary_detect(data, size, &info);
            ASSERT_EQ(info.status, BINARY_MALFORMED);
            ASSERT_EQ(info.architecture.id, ARCH_UNKNOWN);
            ASSERT(!binary_region_at(data, size, &info, 0, &region));
        }
    }
    binary_detect(NULL, 0, &info);
    ASSERT_EQ(info.status, BINARY_RAW);
    ASSERT_EQ(info.architecture.id, ARCH_UNKNOWN);
    memset(data, 0x90, sizeof(data));
    binary_detect(data, sizeof(data), &info);
    ASSERT_EQ(info.status, BINARY_RAW);
    ASSERT_EQ(info.architecture.id, ARCH_UNKNOWN);
    ASSERT_STR_EQ(binary_format_name(info.format), "Raw");
    ASSERT_STR_EQ(binary_format_name(BINARY_FORMAT_MACHO_FAT), "Mach-O universal");
}

int main(void) {
    RUN_TEST(test_elf_architectures_and_addresses);
    RUN_TEST(test_elf_machine_modes);
    RUN_TEST(test_elf_relocatable_sections_and_extended_counts);
    RUN_TEST(test_elf_rejects_overflow_and_invalid_extents);
    RUN_TEST(test_pe_sections_modes_and_bounds);
    RUN_TEST(test_te_header_adjustment);
    RUN_TEST(test_mz_and_extended_dos_formats);
    RUN_TEST(test_macho_endian_sections_and_commands);
    RUN_TEST(test_macho_main_uses_segment_address_and_permissions);
    RUN_TEST(test_nested_sections_limit_segment_mappings);
    RUN_TEST(test_macho_fat_is_explicitly_unsupported);
    RUN_TEST(test_truncated_headers_and_raw_bytes);
    TEST_REPORT();
}
