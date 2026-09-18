#include "lhiew/types.h"
#include "lhiew/architecture.h"
#include "lhiew/editor.h"
#include "lhiew/render.h"

#include <string.h>

#define PROFILE(label, arch, endian) {label, {arch, MODE_LONG_COMPAT_32, endian}}

static const architectureProfile profiles[] = {
    PROFILE("Auto (file header)", ARCH_UNKNOWN, 0),
    {"x86 real 16", {ARCH_X86, REAL, 0}},
    {"x86 16", {ARCH_X86, MODE_LONG_COMPAT_16, 0}},
    {"x86 32", {ARCH_X86, MODE_LONG_COMPAT_32, 0}},
    {"x86 64", {ARCH_X86, MODE_LONG_COMPAT_64, 0}},
    PROFILE("ARM LE", ARCH_ARM, 0),
    PROFILE("ARM BE", ARCH_ARM, 1),
    PROFILE("Thumb LE", ARCH_THUMB, 0),
    PROFILE("Thumb BE", ARCH_THUMB, 1),
    PROFILE("AArch64", ARCH_AARCH64, 0),
    PROFILE("RISC-V32 + C", ARCH_RISCV32, 0),
    PROFILE("RISC-V64 + C", ARCH_RISCV64, 0),
    PROFILE("MIPS32 LE", ARCH_MIPS32, 0),
    PROFILE("MIPS32 BE", ARCH_MIPS32, 1),
    PROFILE("MIPS64 LE", ARCH_MIPS64, 0),
    PROFILE("MIPS64 BE", ARCH_MIPS64, 1),
    PROFILE("PowerPC32 LE", ARCH_PPC32, 0),
    PROFILE("PowerPC32 BE", ARCH_PPC32, 1),
    PROFILE("PowerPC64 LE", ARCH_PPC64, 0),
    PROFILE("PowerPC64 BE", ARCH_PPC64, 1),
    PROFILE("SPARC32 BE", ARCH_SPARC32, 1),
    PROFILE("SPARC V9 BE", ARCH_SPARC64, 1),
    PROFILE("SystemZ BE", ARCH_SYSTEMZ, 1),
    PROFILE("M68K 68040", ARCH_M68K, 1),
    PROFILE("eBPF LE", ARCH_EBPF, 0),
    PROFILE("eBPF BE", ARCH_EBPF, 1),
    PROFILE("SuperH SH4 LE", ARCH_SH, 0),
    PROFILE("SuperH SH4 BE", ARCH_SH, 1),
    PROFILE("TriCore 1.6.2", ARCH_TRICORE, 0),
    PROFILE("XCore", ARCH_XCORE, 0),
    PROFILE("TMS320C64x", ARCH_TMS320C64X, 0),
    PROFILE("Motorola 6809", ARCH_M680X, 1),
    PROFILE("MOS 6502", ARCH_MOS65XX, 0),
};

static binaryInfo detected;
static const uint8_t *detected_file;
static size_t detected_size;

size_t architecture_profile_count(void) {
    return sizeof(profiles) / sizeof(profiles[0]);
}

const architectureProfile *architecture_profile(size_t index) {
    return index < architecture_profile_count() ? &profiles[index] : NULL;
}

const binaryInfo *architecture_binary_info(void) {
    return global_cfg.binary_detected && detected_file == global_cfg.file &&
           detected_size == global_cfg.num_bytes ? &detected : NULL;
}

static void apply_spec(architectureSpec spec) {
    global_cfg.architecture = spec.id;
    global_cfg.disassembler_mode = spec.x86_mode;
    global_cfg.big_endian = spec.big_endian;
    if (global_cfg.disassembler_buffer) {
        memset(global_cfg.disassembler_buffer, 0,
               global_cfg.screenrows * sizeof(disassemblerRow));
    }
}

void architecture_detect_file(void) {
    binary_detect(global_cfg.file, global_cfg.num_bytes, &detected);
    detected_file = global_cfg.file;
    detected_size = global_cfg.num_bytes;
    global_cfg.binary_detected = 1;
    global_cfg.architecture_manual = 0;
    global_cfg.architecture_menu = 0;
    global_cfg.architecture_choice = 0;
    if (detected.status == BINARY_RAW) {
        /* Raw files retain the existing x86 mode until explicitly overridden. */
        architectureSpec raw = {ARCH_X86, global_cfg.disassembler_mode, 0};
        apply_spec(raw);
    } else if (detected.status == BINARY_DETECTED) {
        apply_spec(detected.architecture);
    } else {
        architectureSpec unknown = {ARCH_UNKNOWN, MODE_LONG_COMPAT_32, 0};
        apply_spec(unknown);
    }
}

static size_t matching_profile(void) {
    for (size_t index = 1; index < architecture_profile_count(); ++index) {
        const architectureSpec *spec = &profiles[index].spec;
        if (spec->id == global_cfg.architecture &&
            spec->big_endian == global_cfg.big_endian &&
            (spec->id != ARCH_X86 || spec->x86_mode == global_cfg.disassembler_mode))
            return index;
    }
    return 0;
}

size_t architecture_current_profile(void) {
    return global_cfg.architecture_manual ? matching_profile() : 0;
}

const char *architecture_current_name(void) {
    size_t index = matching_profile();
    return index ? profiles[index].name : "Unsupported";
}

const char *architecture_detection_label(void) {
    const binaryInfo *info = architecture_binary_info();
    if (!info || info->status == BINARY_RAW)
        return "Raw (choose architecture)";
    if (info->status == BINARY_MALFORMED)
        return "Malformed header";
    if (info->status == BINARY_UNSUPPORTED)
        return "Unsupported header/CPU";
    return binary_format_name(info->format);
}

void architecture_select(size_t index) {
    const architectureProfile *profile = architecture_profile(index);
    if (!profile)
        return;
    if (!index) {
        /* Re-detect even if a raw file was previously forced to another CPU. */
        global_cfg.disassembler_mode = MODE_LONG_COMPAT_32;
        architecture_detect_file();
    } else {
        apply_spec(profile->spec);
        global_cfg.architecture_manual = 1;
    }
    editor_set_status_message("%s: %s (%s)", index ? "Manual" : "Auto",
                              architecture_current_name(), architecture_detection_label());
}

void architecture_jump_to_entry(void) {
    const binaryInfo *info = architecture_binary_info();
    if (info && info->has_entry && info->entry_offset < global_cfg.num_bytes) {
        global_cfg.cur_byte = info->entry_offset;
        switch_mode();
    }
}

int architecture_region(size_t offset, binaryRegion *region) {
    const binaryInfo *info = architecture_binary_info();
    return info && (info->status == BINARY_DETECTED ||
                    (info->status == BINARY_UNSUPPORTED && global_cfg.architecture_manual)) &&
           binary_region_at(global_cfg.file, global_cfg.num_bytes, info, offset, region);
}

size_t architecture_alignment(void) {
    switch (global_cfg.architecture) {
        case ARCH_ARM: case ARCH_AARCH64: case ARCH_MIPS32: case ARCH_MIPS64:
        case ARCH_PPC32: case ARCH_PPC64: case ARCH_SPARC32: case ARCH_SPARC64:
        case ARCH_TMS320C64X:
            return 4;
        case ARCH_THUMB: case ARCH_SYSTEMZ: case ARCH_M68K: case ARCH_RISCV32:
        case ARCH_RISCV64: case ARCH_SH: case ARCH_TRICORE: case ARCH_XCORE:
            return 2;
        case ARCH_EBPF:
            return 8;
        default:
            return 1;
    }
}
