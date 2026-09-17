#include "lhiew/types.h"
#include "lhiew/binary.h"

#include <string.h>

/* Read serialized headers without alignment or host-endianness assumptions. */
static uint16_t read16(const uint8_t *p, int be) {
    return be ? (uint16_t)((uint16_t)p[0] << 8 | p[1])
              : (uint16_t)((uint16_t)p[1] << 8 | p[0]);
}

static uint32_t read32(const uint8_t *p, int be) {
    return be ? (uint32_t)read16(p, be) << 16 | read16(p + 2, be)
              : (uint32_t)read16(p + 2, be) << 16 | read16(p, be);
}

static uint64_t read64(const uint8_t *p, int be) {
    return be ? (uint64_t)read32(p, be) << 32 | read32(p + 4, be)
              : (uint64_t)read32(p + 4, be) << 32 | read32(p, be);
}

static int span(size_t size, uint64_t offset, uint64_t length) {
    return offset <= size && length <= size - (size_t)offset;
}

static int table_span(size_t size, uint64_t offset, uint64_t count,
                      uint64_t stride, size_t minimum_stride) {
    if (!count) return offset <= size;
    return stride >= minimum_stride && offset <= size &&
           count <= (size - (size_t)offset) / stride;
}

static int set_region(size_t size, uint64_t offset, uint64_t length,
                      uint64_t address, int executable, binaryRegion *region) {
    if (!length || !span(size, offset, length) || length - 1 > UINT64_MAX - address)
        return 0;
    *region = (binaryRegion){(size_t)offset, (size_t)length, address, executable};
    return 1;
}

static int contains(const binaryRegion *region, size_t offset) {
    return offset >= region->offset && offset - region->offset < region->size;
}

static void set_architecture(binaryInfo *info, architectureId id, int be) {
    info->architecture.id = id;
    info->architecture.big_endian = be;
    info->architecture.x86_mode = MODE_LONG_COMPAT_32;
}

static int supported_byte_order(architectureSpec architecture) {
    switch (architecture.id) {
        case ARCH_SPARC32: case ARCH_SPARC64: case ARCH_SYSTEMZ: case ARCH_M68K:
            return architecture.big_endian;
        case ARCH_RISCV32: case ARCH_RISCV64: case ARCH_TRICORE:
        case ARCH_XCORE: case ARCH_TMS320C64X:
            return !architecture.big_endian;
        default:
            return 1;
    }
}

static void elf_architecture(binaryInfo *info, uint16_t machine, uint32_t flags,
                             uint64_t entry) {
    int wide = info->container_64;
    int be = info->container_big_endian;
    architectureId id = ARCH_UNKNOWN;
    switch (machine) {
        case 2: case 18: id = ARCH_SPARC32; break;
        case 3: case 6: id = ARCH_X86; be = 0; break;
        case 4: id = ARCH_M68K; break;
        case 8: case 10: {
            unsigned isa = flags >> 28;
            wide = wide || isa == 2 || isa == 3 || isa == 4 || isa == 6 || isa == 8;
            /* MIPS16, microMIPS and release 6 need distinct decoder profiles. */
            if (!(flags & 0x06000000) && isa != 9 && isa != 10)
                id = wide ? ARCH_MIPS64 : ARCH_MIPS32;
            break;
        }
        case 20: id = ARCH_PPC32; break;
        case 21: id = ARCH_PPC64; break;
        case 22: id = ARCH_SYSTEMZ; break;
        case 40:
            id = entry & 1 ? ARCH_THUMB : ARCH_ARM;
            if (flags & 0x00800000) be = 0; /* ARM BE8 instruction encoding. */
            break;
        case 42: id = ARCH_SH; break;
        case 43: id = ARCH_SPARC64; break;
        case 44: id = ARCH_TRICORE; break;
        case 62: id = ARCH_X86; be = 0; break;
        case 140: id = ARCH_TMS320C64X; break;
        case 183: id = ARCH_AARCH64; be = 0; break;
        case 203: id = ARCH_XCORE; break;
        case 243: id = wide || (flags & 0x20) ? ARCH_RISCV64 : ARCH_RISCV32; break;
        case 247: id = ARCH_EBPF; break;
    }
    set_architecture(info, id, be);
    if (machine == 62) info->architecture.x86_mode = MODE_LONG_COMPAT_64;
}

static int elf_section(const uint8_t *data, size_t size, const binaryInfo *info,
                       size_t index, binaryRegion *region) {
    const uint8_t *p = data + info->secondary_offset + index * info->secondary_stride;
    int be = info->container_big_endian;
    uint32_t type = read32(p + 4, be);
    if (!type || type == 8) return 0; /* SHT_NULL and SHT_NOBITS have no bytes. */
    uint64_t flags = info->container_64 ? read64(p + 8, be) : read32(p + 8, be);
    uint64_t address = info->container_64 ? read64(p + 16, be) : read32(p + 12, be);
    uint64_t offset = info->container_64 ? read64(p + 24, be) : read32(p + 16, be);
    uint64_t length = info->container_64 ? read64(p + 32, be) : read32(p + 20, be);
    return set_region(size, offset, length, address, !!(flags & 4), region);
}

static int elf_segment(const uint8_t *data, size_t size, const binaryInfo *info,
                       size_t index, binaryRegion *region) {
    const uint8_t *p = data + info->table_offset + index * info->table_stride;
    int be = info->container_big_endian;
    if (read32(p, be) != 1) return 0; /* PT_LOAD */
    uint64_t offset = info->container_64 ? read64(p + 8, be) : read32(p + 4, be);
    uint64_t address = info->container_64 ? read64(p + 16, be) : read32(p + 8, be);
    uint64_t length = info->container_64 ? read64(p + 32, be) : read32(p + 16, be);
    uint32_t flags = read32(p + (info->container_64 ? 4 : 24), be);
    return set_region(size, offset, length, address, !!(flags & 1), region);
}

static void choose_entry(binaryInfo *info, const binaryRegion *region,
                         uint64_t address, int *exact) {
    if (region->executable && !info->has_entry) {
        info->has_entry = 1;
        info->entry_offset = region->offset;
    }
    if (!*exact && region->executable && address >= region->address &&
        address - region->address < region->size) {
        info->has_entry = 1;
        info->entry_offset = region->offset + (size_t)(address - region->address);
        *exact = 1;
    }
}

static int detect_elf(const uint8_t *data, size_t size, binaryInfo *info) {
    info->format = BINARY_FORMAT_ELF;
    if (size < 16 || (data[4] != 1 && data[4] != 2) ||
        (data[5] != 1 && data[5] != 2) || data[6] != 1) return 0;
    info->container_64 = data[4] == 2;
    info->container_big_endian = data[5] == 2;
    int wide = info->container_64;
    int be = info->container_big_endian;
    size_t header = wide ? 64 : 52;
    if (size < header || read32(data + 20, be) != 1 ||
        read16(data + (wide ? 52 : 40), be) < header ||
        read16(data + (wide ? 52 : 40), be) > size) return 0;
    uint64_t entry = wide ? read64(data + 24, be) : read32(data + 24, be);
    uint64_t phoff = wide ? read64(data + 32, be) : read32(data + 28, be);
    uint64_t shoff = wide ? read64(data + 40, be) : read32(data + 32, be);
    uint32_t flags = read32(data + (wide ? 48 : 36), be);
    uint64_t phnum = read16(data + (wide ? 56 : 44), be);
    uint64_t shnum = read16(data + (wide ? 60 : 48), be);
    size_t phstride = read16(data + (wide ? 54 : 42), be);
    size_t shstride = read16(data + (wide ? 58 : 46), be);
    if ((!shnum && shoff) || phnum == 0xffff) {
        if (!shoff || !table_span(size, shoff, 1, shstride, wide ? 64 : 40)) return 0;
        const uint8_t *zero = data + (size_t)shoff;
        if (read32(zero + 4, be)) return 0;
        if (!shnum) shnum = wide ? read64(zero + 32, be) : read32(zero + 20, be);
        if (phnum == 0xffff) phnum = read32(zero + (wide ? 44 : 28), be);
    }
    if ((phnum && !phoff) || (shnum && !shoff) ||
        !table_span(size, phoff, phnum, phstride, wide ? 56 : 32) ||
        !table_span(size, shoff, shnum, shstride, wide ? 64 : 40)) return 0;
    info->table_offset = (size_t)phoff;
    info->table_count = (size_t)phnum;
    info->table_stride = phstride;
    info->secondary_offset = (size_t)shoff;
    info->secondary_count = (size_t)shnum;
    info->secondary_stride = shstride;
    elf_architecture(info, read16(data + 18, be), flags, entry);
    if (info->architecture.id == ARCH_THUMB) entry &= ~(uint64_t)1;
    int exact = 0;
    /* Sections offer tighter instruction-alignment boundaries than segments. */
    for (size_t i = 0; i < info->secondary_count; ++i) {
        const uint8_t *p = data + info->secondary_offset + i * shstride;
        uint32_t type = read32(p + 4, be);
        if (!type || type == 8) continue;
        uint64_t offset = wide ? read64(p + 24, be) : read32(p + 16, be);
        uint64_t length = wide ? read64(p + 32, be) : read32(p + 20, be);
        uint64_t address = wide ? read64(p + 16, be) : read32(p + 12, be);
        if (!span(size, offset, length) || (length && length - 1 > UINT64_MAX - address)) return 0;
        binaryRegion region;
        if (elf_section(data, size, info, i, &region)) choose_entry(info, &region, entry, &exact);
    }
    for (size_t i = 0; i < info->table_count; ++i) {
        const uint8_t *p = data + info->table_offset + i * phstride;
        uint64_t offset = wide ? read64(p + 8, be) : read32(p + 4, be);
        uint64_t length = wide ? read64(p + 32, be) : read32(p + 16, be);
        uint64_t memsize = wide ? read64(p + 40, be) : read32(p + 20, be);
        uint64_t address = wide ? read64(p + 16, be) : read32(p + 8, be);
        if (!span(size, offset, length) || (length && length - 1 > UINT64_MAX - address) ||
            (read32(p, be) == 1 && length > memsize)) return 0;
        binaryRegion region;
        if (elf_segment(data, size, info, i, &region)) choose_entry(info, &region, entry, &exact);
    }
    return 1;
}

static void pe_architecture(binaryInfo *info, uint16_t machine) {
    architectureId id = ARCH_UNKNOWN;
    switch (machine) {
        case 0x14c: case 0x8664: id = ARCH_X86; break;
        case 0x1c0: id = ARCH_ARM; break;
        case 0x1c2: case 0x1c4: id = ARCH_THUMB; break;
        case 0xaa64: id = ARCH_AARCH64; break;
        case 0x166: case 0x169: id = ARCH_MIPS32; break;
        case 0x1f0: case 0x1f1: id = ARCH_PPC32; break;
        case 0x1a2: case 0x1a3: case 0x1a6: case 0x1a8: id = ARCH_SH; break;
        case 0x5032: id = ARCH_RISCV32; break;
        case 0x5064: id = ARCH_RISCV64; break;
    }
    set_architecture(info, id, 0);
    if (machine == 0x8664) info->architecture.x86_mode = MODE_LONG_COMPAT_64;
}

static int pe_section(const uint8_t *data, size_t size, const binaryInfo *info,
                      size_t index, binaryRegion *region) {
    const uint8_t *p = data + info->table_offset + index * 40;
    uint64_t offset = read32(p + 20, 0);
    uint64_t length = read32(p + 16, 0);
    uint64_t virtual_size = read32(p + 8, 0);
    uint64_t address = read32(p + 12, 0);
    if (offset < info->stripped_size || address > UINT64_MAX - info->image_base) return 0;
    offset -= info->stripped_size;
    if (virtual_size && virtual_size < length) length = virtual_size;
    return set_region(size, offset, length, info->image_base + address,
                      !!(read32(p + 36, 0) & 0x20000000), region);
}

static int validate_pe_sections(const uint8_t *data, size_t size, binaryInfo *info,
                                uint64_t entry) {
    if (!table_span(size, info->table_offset, info->table_count, 40, 40)) return 0;
    int exact = 0;
    for (size_t i = 0; i < info->table_count; ++i) {
        const uint8_t *p = data + info->table_offset + i * 40;
        uint64_t length = read32(p + 16, 0);
        uint64_t offset = read32(p + 20, 0);
        uint64_t address = read32(p + 12, 0);
        if (length && (offset < info->stripped_size ||
            !span(size, offset - info->stripped_size, length))) return 0;
        if (address > UINT64_MAX - info->image_base ||
            (length && length - 1 > UINT64_MAX - (info->image_base + address))) return 0;
        binaryRegion region;
        if (pe_section(data, size, info, i, &region)) choose_entry(info, &region, entry, &exact);
    }
    return 1;
}

static int detect_pe(const uint8_t *data, size_t size, size_t offset, binaryInfo *info) {
    info->format = BINARY_FORMAT_PE;
    if (!span(size, offset, 24)) return 0;
    const uint8_t *coff = data + offset + 4;
    size_t optional_size = read16(coff + 16, 0);
    if (!span(size, offset + 24, optional_size) || optional_size < 2) return 0;
    const uint8_t *optional = data + offset + 24;
    unsigned magic = read16(optional, 0);
    if (magic != 0x10b && magic != 0x20b) return 0;
    int wide = magic == 0x20b;
    if (optional_size < (wide ? 112 : 96)) return 0;
    uint32_t directories = read32(optional + (wide ? 108 : 92), 0);
    if (directories > (optional_size - (wide ? 112 : 96)) / 8) return 0;
    info->container_64 = wide;
    info->table_offset = offset + 24 + optional_size;
    info->table_count = read16(coff + 2, 0);
    info->table_stride = 40;
    info->image_base = wide ? read64(optional + 24, 0) : read32(optional + 28, 0);
    uint64_t entry = read32(optional + 16, 0);
    if (entry > UINT64_MAX - info->image_base) return 0;
    pe_architecture(info, read16(coff, 0));
    return validate_pe_sections(data, size, info, info->image_base + entry);
}

static int detect_te(const uint8_t *data, size_t size, binaryInfo *info) {
    info->format = BINARY_FORMAT_TE;
    if (size < 40 || read16(data + 6, 0) < 40) return 0;
    info->stripped_size = read16(data + 6, 0) - 40;
    info->table_offset = 40;
    info->table_count = data[4];
    info->table_stride = 40;
    info->image_base = read64(data + 16, 0);
    uint64_t entry = read32(data + 8, 0);
    if (entry > UINT64_MAX - info->image_base) return 0;
    pe_architecture(info, read16(data + 2, 0));
    return validate_pe_sections(data, size, info, info->image_base + entry);
}

static int detect_mz(const uint8_t *data, size_t size, binaryInfo *info) {
    info->format = BINARY_FORMAT_MZ;
    if (size < 28) return 0;
    if (size >= 64) {
        uint32_t new_header = read32(data + 60, 0);
        if (new_header >= 64) {
            if (!span(size, new_header, 2)) return 0;
            const uint8_t *p = data + new_header;
            if (p[0] == 'P' && p[1] == 'E') {
                info->format = BINARY_FORMAT_PE;
                if (!span(size, new_header, 4) || p[2] || p[3]) return 0;
                return detect_pe(data, size, new_header, info);
            }
            if ((p[0] == 'N' || p[0] == 'L') && (p[1] == 'E' || p[1] == 'X')) {
                if (p[0] == 'N' && p[1] == 'E') info->format = BINARY_FORMAT_NE;
                else if (p[0] == 'L' && p[1] == 'E') info->format = BINARY_FORMAT_LE;
                else if (p[0] == 'L' && p[1] == 'X') info->format = BINARY_FORMAT_LX;
                else return 0;
                return 1;
            }
        }
    }
    size_t pages = read16(data + 4, 0);
    size_t last_page = read16(data + 2, 0);
    size_t header = (size_t)read16(data + 8, 0) * 16;
    if (!pages || last_page >= 512 || header < 28) return 0;
    size_t image_size = (pages - 1) * 512 + (last_page ? last_page : 512);
    if (image_size > size || header > image_size) return 0;
    size_t relocations = read16(data + 6, 0);
    size_t relocation_offset = read16(data + 24, 0);
    if (relocations && !table_span(header, relocation_offset, relocations, 4, 4)) return 0;
    info->data_offset = header;
    info->data_size = image_size - header;
    set_architecture(info, ARCH_X86, 0);
    info->architecture.x86_mode = REAL;
    size_t entry = (size_t)read16(data + 22, 0) * 16 + read16(data + 20, 0);
    if (entry < info->data_size) {
        info->has_entry = 1;
        info->entry_offset = header + entry;
    } else if (info->data_size) {
        info->has_entry = 1;
        info->entry_offset = header;
    }
    return 1;
}

static void macho_architecture(binaryInfo *info, uint32_t cpu) {
    architectureId id = ARCH_UNKNOWN;
    switch (cpu) {
        case 7: case 0x01000007: id = ARCH_X86; break;
        case 6: id = ARCH_M68K; break;
        case 12: id = ARCH_ARM; break;
        case 0x0100000c: case 0x0200000c: id = ARCH_AARCH64; break;
        case 18: id = ARCH_PPC32; break;
        case 0x01000012: id = ARCH_PPC64; break;
        case 14: id = ARCH_SPARC32; break;
    }
    set_architecture(info, id, info->container_big_endian);
    if (id == ARCH_AARCH64 || id == ARCH_X86) info->architecture.big_endian = 0;
    if (cpu == 0x01000007) info->architecture.x86_mode = MODE_LONG_COMPAT_64;
}

static int macho_segment(const uint8_t *p, size_t size, int be, int wide,
                         binaryRegion *region) {
    uint64_t address = wide ? read64(p + 24, be) : read32(p + 24, be);
    uint64_t offset = wide ? read64(p + 40, be) : read32(p + 32, be);
    uint64_t length = wide ? read64(p + 48, be) : read32(p + 36, be);
    return set_region(size, offset, length, address,
                      !!(read32(p + (wide ? 60 : 44), be) & 4), region);
}

static int macho_section(const uint8_t *p, size_t size, int be, int wide,
                         binaryRegion *region) {
    uint64_t address = wide ? read64(p + 32, be) : read32(p + 32, be);
    uint64_t length = wide ? read64(p + 40, be) : read32(p + 36, be);
    uint32_t offset = read32(p + (wide ? 48 : 40), be);
    uint32_t flags = read32(p + (wide ? 64 : 56), be);
    unsigned type = flags & 0xff;
    if (type == 1 || type == 12 || type == 18) return 0; /* Zerofill sections. */
    return set_region(size, offset, length, address, !!(flags & 0x80000400), region);
}

static int detect_macho(const uint8_t *data, size_t size, binaryInfo *info,
                         uint32_t magic) {
    info->format = BINARY_FORMAT_MACHO;
    int be = magic == 0xcefaedfe || magic == 0xcffaedfe;
    int wide = magic == 0xfeedfacf || magic == 0xcffaedfe;
    size_t header = wide ? 32 : 28;
    if (size < header) return 0;
    info->container_big_endian = be;
    info->container_64 = wide;
    info->table_offset = header;
    info->table_count = read32(data + 16, be);
    info->data_size = read32(data + 20, be);
    if (!span(size, header, info->data_size) || info->table_count > info->data_size / 8) return 0;
    macho_architecture(info, read32(data + 4, be));
    size_t offset = header;
    size_t end = header + info->data_size;
    int have_main = 0;
    uint64_t main_relative_offset = 0;
    int have_text = 0;
    uint64_t text_address = 0;
    for (size_t i = 0; i < info->table_count; ++i) {
        if (!span(end, offset, 8)) return 0;
        const uint8_t *p = data + offset;
        uint32_t command = read32(p, be);
        size_t length = read32(p + 4, be);
        if (length < 8 || !span(end, offset, length)) return 0;
        if (command == 1 || command == 0x19) {
            int segment64 = command == 0x19;
            size_t fixed = segment64 ? 72 : 56;
            size_t stride = segment64 ? 80 : 68;
            if (length < fixed) return 0;
            size_t sections = read32(p + (segment64 ? 64 : 48), be);
            if (sections > (length - fixed) / stride) return 0;
            uint64_t fileoff = segment64 ? read64(p + 40, be) : read32(p + 32, be);
            uint64_t filesize = segment64 ? read64(p + 48, be) : read32(p + 36, be);
            uint64_t address = segment64 ? read64(p + 24, be) : read32(p + 24, be);
            if (!span(size, fileoff, filesize) || (filesize && filesize - 1 > UINT64_MAX - address)) return 0;
            if (!have_text && !memcmp(p + 8, "__TEXT\0", 7)) {
                have_text = 1;
                text_address = address;
            }
            for (size_t j = 0; j < sections; ++j) {
                const uint8_t *s = p + fixed + j * stride;
                uint64_t section_length = segment64 ? read64(s + 40, be) : read32(s + 36, be);
                uint64_t section_address = segment64 ? read64(s + 32, be) : read32(s + 32, be);
                uint32_t section_offset = read32(s + (segment64 ? 48 : 40), be);
                unsigned type = read32(s + (segment64 ? 64 : 56), be) & 0xff;
                if (type != 1 && type != 12 && type != 18 &&
                    (!span(size, section_offset, section_length) ||
                     (section_length && section_length - 1 > UINT64_MAX - section_address))) return 0;
                binaryRegion region;
                if (!info->has_entry && macho_section(s, size, be, segment64, &region) && region.executable) {
                    info->has_entry = 1;
                    info->entry_offset = region.offset;
                }
            }
            binaryRegion region;
            if (!info->has_entry && macho_segment(p, size, be, segment64, &region) && region.executable) {
                info->has_entry = 1;
                info->entry_offset = region.offset;
            }
        } else if (command == 0x80000028) { /* LC_MAIN */
            if (length < 24 || have_main) return 0;
            main_relative_offset = read64(p + 8, be);
            have_main = 1;
        }
        offset += length;
    }
    if (offset != end) return 0;
    if (have_main) {
        /* dyld interprets LC_MAIN relative to __TEXT's virtual address, then
           verifies segment permissions. Section instruction flags are hints. */
        if (!have_text || main_relative_offset > UINT64_MAX - text_address) return 0;
        uint64_t entry_address = text_address + main_relative_offset;
        int found = 0;
        offset = header;
        for (size_t i = 0; i < info->table_count; ++i) {
            const uint8_t *p = data + offset;
            uint32_t command = read32(p, be);
            binaryRegion region;
            if ((command == 1 || command == 0x19) &&
                macho_segment(p, size, be, command == 0x19, &region) && region.executable &&
                entry_address >= region.address && entry_address - region.address < region.size) {
                info->has_entry = 1;
                info->entry_offset = region.offset + (size_t)(entry_address - region.address);
                found = 1;
                break;
            }
            offset += read32(p + 4, be);
        }
        if (!found) return 0;
    }
    return 1;
}

static int detect_fat(const uint8_t *data, size_t size, binaryInfo *info, uint32_t magic) {
    info->format = BINARY_FORMAT_MACHO_FAT;
    int be = magic == 0xbebafeca || magic == 0xbfbafeca;
    int wide = magic == 0xcafebabf || magic == 0xbfbafeca;
    if (size < 8) return 0;
    size_t count = read32(data + 4, be);
    size_t stride = wide ? 32 : 20;
    if (!count || !table_span(size, 8, count, stride, stride)) return 0;
    for (size_t i = 0; i < count; ++i) {
        const uint8_t *p = data + 8 + i * stride;
        uint64_t offset = wide ? read64(p + 8, be) : read32(p + 8, be);
        uint64_t length = wide ? read64(p + 16, be) : read32(p + 12, be);
        uint32_t align = read32(p + (wide ? 24 : 16), be);
        if (!length || !span(size, offset, length) || align > 63 ||
            (offset & ((UINT64_C(1) << align) - 1))) return 0;
    }
    return 1; /* A universal binary needs an explicit slice selection. */
}

static int detect_nlm(const uint8_t *data, size_t size, binaryInfo *info) {
    info->format = BINARY_FORMAT_NLM;
    if (size < 24) return 0;
    if (memcmp(data, "NetWare Loadable Module\x1a", 24)) return 1;
    if (size < 130) return 0;
    if (read32(data + 24, 0) != 4) return 1;
    if (!data[28] || data[28] > 12 || data[29 + data[28]] ||
        memchr(data + 29, 0, data[28])) return 0;
    size_t header_end = 130;
    const size_t maximum[] = {127, 71, 17};
    for (size_t i = 0; i < 3; ++i) {
        if (!span(size, header_end, 1)) return 0;
        size_t length = data[header_end++];
        if (length > maximum[i] || !span(size, header_end, length + 1) ||
            data[header_end + length]) return 0;
        header_end += length + 1;
        if (!i) {
            if (!span(size, header_end, 14)) return 0;
            header_end += 14;
        }
    }
    info->data_offset = read32(data + 42, 0);
    info->data_size = read32(data + 46, 0);
    info->table_offset = read32(data + 50, 0);
    info->table_count = read32(data + 54, 0);
    if (!span(size, info->data_offset, info->data_size) ||
        !span(size, info->table_offset, info->table_count)) return 0;
    if ((info->data_size && info->data_offset < header_end) ||
        (info->table_count && info->table_offset < header_end) ||
        (info->data_size && info->table_count &&
         info->data_offset < info->table_offset + info->table_count &&
         info->table_offset < info->data_offset + info->data_size)) return 0;
    uint32_t entry = read32(data + 110, 0);
    if (info->data_size ? entry >= info->data_size : entry != 0) return 0;
    set_architecture(info, ARCH_X86, 0);
    info->architecture.x86_mode = MODE_LONG_COMPAT_32;
    if (info->data_size) {
        info->has_entry = 1;
        info->entry_offset = info->data_offset + entry;
    }
    return 1;
}

void binary_detect(const uint8_t *data, size_t size, binaryInfo *info) {
    if (!info) return;
    memset(info, 0, sizeof(*info));
    set_architecture(info, ARCH_UNKNOWN, 0);
    if (!data || !size) return;
    int valid = 1;
    if (size >= 4 && !memcmp(data, "\x7f" "ELF", 4)) valid = detect_elf(data, size, info);
    else if (size >= 8 && !memcmp(data, "NetWare ", 8)) valid = detect_nlm(data, size, info);
    else if (size >= 2 && data[0] == 'M' && data[1] == 'Z') valid = detect_mz(data, size, info);
    else if (size >= 2 && data[0] == 'V' && data[1] == 'Z') valid = detect_te(data, size, info);
    else if (size >= 4) {
        uint32_t magic = read32(data, 0);
        if (magic == 0xfeedface || magic == 0xfeedfacf || magic == 0xcefaedfe || magic == 0xcffaedfe)
            valid = detect_macho(data, size, info, magic);
        else if (magic == 0xcafebabe || magic == 0xcafebabf || magic == 0xbebafeca || magic == 0xbfbafeca)
            valid = detect_fat(data, size, info, magic);
    }
    if (!valid) {
        info->status = BINARY_MALFORMED;
        info->architecture.id = ARCH_UNKNOWN;
        info->has_entry = 0;
    } else if (info->format != BINARY_FORMAT_RAW) {
        if (!supported_byte_order(info->architecture)) info->architecture.id = ARCH_UNKNOWN;
        info->status = info->architecture.id == ARCH_UNKNOWN ? BINARY_UNSUPPORTED : BINARY_DETECTED;
    }
}

/* Split broad segment mappings at section boundaries. The returned origin and
   extent must describe one contiguous mapping so lookback and decoding cannot
   cross into a section with a different address or instruction alignment. */
static void clip_region(binaryRegion *region, const binaryRegion *other, size_t offset) {
    if (other->offset > offset && other->offset > region->offset &&
        other->offset - region->offset < region->size) {
        region->size = other->offset - region->offset;
    } else if (offset >= other->offset && offset - other->offset >= other->size) {
        size_t end = other->offset + other->size;
        if (end > region->offset && end - region->offset < region->size) {
            size_t delta = end - region->offset;
            region->offset = end;
            region->address += delta;
            region->size -= delta;
        }
    }
}

static int finish_region(const uint8_t *data, size_t size, const binaryInfo *info,
                         size_t offset, binaryRegion candidate, binaryRegion *region) {
    binaryRegion other;
    if (info->format == BINARY_FORMAT_ELF) {
        for (size_t i = 0; i < info->secondary_count; ++i)
            if (elf_section(data, size, info, i, &other)) clip_region(&candidate, &other, offset);
    } else if (info->format == BINARY_FORMAT_PE || info->format == BINARY_FORMAT_TE) {
        for (size_t i = 0; i < info->table_count; ++i)
            if (pe_section(data, size, info, i, &other)) clip_region(&candidate, &other, offset);
    } else if (info->format == BINARY_FORMAT_MACHO) {
        int be = info->container_big_endian;
        size_t command_offset = info->table_offset;
        for (size_t i = 0; i < info->table_count; ++i) {
            const uint8_t *p = data + command_offset;
            uint32_t command = read32(p, be);
            if (command == 1 || command == 0x19) {
                int wide = command == 0x19;
                binaryRegion segment;
                if (macho_segment(p, size, be, wide, &segment) &&
                    contains(&segment, offset) && segment.executable)
                    candidate.executable = 1;
                size_t count = read32(p + (wide ? 64 : 48), be);
                size_t fixed = wide ? 72 : 56;
                size_t stride = wide ? 80 : 68;
                for (size_t j = 0; j < count; ++j)
                    if (macho_section(p + fixed + j * stride, size, be, wide, &other))
                        clip_region(&candidate, &other, offset);
            }
            command_offset += read32(p + 4, be);
        }
    }
    *region = candidate;
    return 1;
}

int binary_region_at(const uint8_t *data, size_t size, const binaryInfo *info,
                     size_t offset, binaryRegion *region) {
    if (!data || !info || !region || offset >= size ||
        (info->status != BINARY_DETECTED && info->status != BINARY_UNSUPPORTED)) return 0;
    binaryRegion candidate;
    if (info->format == BINARY_FORMAT_ELF) {
        for (size_t i = 0; i < info->secondary_count; ++i)
            if (elf_section(data, size, info, i, &candidate) && contains(&candidate, offset)) {
                return finish_region(data, size, info, offset, candidate, region);
            }
        for (size_t i = 0; i < info->table_count; ++i)
            if (elf_segment(data, size, info, i, &candidate) && contains(&candidate, offset)) {
                return finish_region(data, size, info, offset, candidate, region);
            }
    } else if (info->format == BINARY_FORMAT_PE || info->format == BINARY_FORMAT_TE) {
        for (size_t i = 0; i < info->table_count; ++i)
            if (pe_section(data, size, info, i, &candidate) && contains(&candidate, offset)) {
                return finish_region(data, size, info, offset, candidate, region);
            }
    } else if (info->format == BINARY_FORMAT_MZ) {
        if (set_region(size, info->data_offset, info->data_size, 0, 1, &candidate) && contains(&candidate, offset)) {
            return finish_region(data, size, info, offset, candidate, region);
        }
    } else if (info->format == BINARY_FORMAT_NLM) {
        /* NLM load bases are supplied by the runtime; display file-relative
           addresses while keeping code/data decoding within their image spans. */
        if (set_region(size, info->data_offset, info->data_size, info->data_offset, 1, &candidate) &&
            contains(&candidate, offset)) {
            *region = candidate;
            return 1;
        }
        if (set_region(size, info->table_offset, info->table_count, info->table_offset, 0, &candidate) &&
            contains(&candidate, offset)) {
            *region = candidate;
            return 1;
        }
    } else if (info->format == BINARY_FORMAT_MACHO) {
        int be = info->container_big_endian;
        /* Prefer sections, then segments, without retaining pointers to file bytes. */
        for (int sections_first = 1; sections_first >= 0; --sections_first) {
            size_t command_offset = info->table_offset;
            for (size_t i = 0; i < info->table_count; ++i) {
                const uint8_t *p = data + command_offset;
                uint32_t command = read32(p, be);
                if (command == 1 || command == 0x19) {
                    int wide = command == 0x19;
                    if (sections_first) {
                        size_t count = read32(p + (wide ? 64 : 48), be);
                        size_t fixed = wide ? 72 : 56;
                        size_t stride = wide ? 80 : 68;
                        for (size_t j = 0; j < count; ++j)
                            if (macho_section(p + fixed + j * stride, size, be, wide, &candidate) && contains(&candidate, offset)) {
                                return finish_region(data, size, info, offset, candidate, region);
                            }
                    } else if (macho_segment(p, size, be, wide, &candidate) && contains(&candidate, offset)) {
                        return finish_region(data, size, info, offset, candidate, region);
                    }
                }
                command_offset += read32(p + 4, be);
            }
        }
    }
    return 0;
}

const char *binary_format_name(binaryFormat format) {
    switch (format) {
        case BINARY_FORMAT_ELF: return "ELF";
        case BINARY_FORMAT_PE: return "PE";
        case BINARY_FORMAT_TE: return "TE";
        case BINARY_FORMAT_MACHO: return "Mach-O";
        case BINARY_FORMAT_MACHO_FAT: return "Mach-O universal";
        case BINARY_FORMAT_MZ: return "DOS MZ";
        case BINARY_FORMAT_NE: return "NE";
        case BINARY_FORMAT_LE: return "LE";
        case BINARY_FORMAT_LX: return "LX";
        case BINARY_FORMAT_NLM: return "NLM";
        default: return "Raw";
    }
}
