#include "lhiew/types.h"
#include "lhiew/disassembler.h"
#include "lhiew/architecture.h"
#include "lhiew/terminal.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "Zydis/Zydis.h"
#include <capstone/capstone.h>

#define DISASSEMBLY_LOOKBACK 128
#define MAX_INSTRUCTION_BYTES 24

typedef struct instructionDecoder {
    ZydisDecoder zydis;
    ZydisFormatter formatter;
    csh capstone;
    cs_insn *instruction;
    const binaryInfo *info;
    size_t alignment;
    int enabled;
} instructionDecoder;

static int decoder_init(instructionDecoder *decoder) {
    memset(decoder, 0, sizeof(*decoder));
    decoder->info = architecture_binary_info();
    decoder->alignment = architecture_alignment();
    if (global_cfg.architecture == ARCH_UNKNOWN)
        return 1;
    if (global_cfg.architecture == ARCH_X86) {
        static const ZydisMachineMode modes[] = {
            ZYDIS_MACHINE_MODE_REAL_16, ZYDIS_MACHINE_MODE_LONG_COMPAT_16,
            ZYDIS_MACHINE_MODE_LONG_COMPAT_32, ZYDIS_MACHINE_MODE_LONG_64
        };
        static const ZydisStackWidth widths[] = {
            ZYDIS_STACK_WIDTH_16, ZYDIS_STACK_WIDTH_16,
            ZYDIS_STACK_WIDTH_32, ZYDIS_STACK_WIDTH_64
        };
        if ((unsigned)global_cfg.disassembler_mode >= sizeof(modes) / sizeof(modes[0]))
            return 0;
        if (!ZYAN_SUCCESS(ZydisDecoderInit(&decoder->zydis,
                         modes[global_cfg.disassembler_mode], widths[global_cfg.disassembler_mode])) ||
            !ZYAN_SUCCESS(ZydisFormatterInit(&decoder->formatter, ZYDIS_FORMATTER_STYLE_ATT)))
            return 0;
    } else {
        cs_arch arch;
        cs_mode mode = global_cfg.big_endian ? CS_MODE_BIG_ENDIAN : CS_MODE_LITTLE_ENDIAN;
        switch (global_cfg.architecture) {
            case ARCH_ARM: arch = CS_ARCH_ARM; mode |= CS_MODE_ARM; break;
            case ARCH_THUMB: arch = CS_ARCH_ARM; mode |= CS_MODE_THUMB; break;
            case ARCH_AARCH64: arch = CS_ARCH_ARM64; mode = CS_MODE_LITTLE_ENDIAN; break;
            case ARCH_MIPS32: arch = CS_ARCH_MIPS; mode |= CS_MODE_MIPS32; break;
            case ARCH_MIPS64: arch = CS_ARCH_MIPS; mode |= CS_MODE_MIPS64; break;
            case ARCH_PPC32: arch = CS_ARCH_PPC; mode |= CS_MODE_32; break;
            case ARCH_PPC64: arch = CS_ARCH_PPC; mode |= CS_MODE_64; break;
            case ARCH_SPARC32: arch = CS_ARCH_SPARC; break;
            case ARCH_SPARC64: arch = CS_ARCH_SPARC; mode |= CS_MODE_V9; break;
            case ARCH_SYSTEMZ: arch = CS_ARCH_SYSZ; break;
            case ARCH_M68K: arch = CS_ARCH_M68K; mode |= CS_MODE_M68K_040; break;
            case ARCH_RISCV32: arch = CS_ARCH_RISCV; mode |= CS_MODE_RISCV32 | CS_MODE_RISCVC; break;
            case ARCH_RISCV64: arch = CS_ARCH_RISCV; mode |= CS_MODE_RISCV64 | CS_MODE_RISCVC; break;
            case ARCH_EBPF: arch = CS_ARCH_BPF; mode |= CS_MODE_BPF_EXTENDED; break;
            case ARCH_SH: arch = CS_ARCH_SH; mode |= CS_MODE_SH4 | CS_MODE_SHFPU; break;
            case ARCH_TRICORE: arch = CS_ARCH_TRICORE; mode |= CS_MODE_TRICORE_162; break;
            case ARCH_XCORE: arch = CS_ARCH_XCORE; break;
            case ARCH_TMS320C64X: arch = CS_ARCH_TMS320C64X; break;
            case ARCH_M680X: arch = CS_ARCH_M680X; mode = CS_MODE_M680X_6809; break;
            case ARCH_MOS65XX: arch = CS_ARCH_MOS65XX; mode = CS_MODE_MOS65XX_6502; break;
            default: return 0;
        }
        if (cs_open(arch, mode, &decoder->capstone) != CS_ERR_OK)
            return 0;
        decoder->instruction = cs_malloc(decoder->capstone);
        if (!decoder->instruction) {
            cs_close(&decoder->capstone);
            return 0;
        }
    }
    decoder->enabled = 1;
    return 1;
}

static void decoder_free(instructionDecoder *decoder) {
    if (decoder->instruction)
        cs_free(decoder->instruction, 1);
    if (decoder->capstone)
        cs_close(&decoder->capstone);
}

static size_t decode_instruction(instructionDecoder *decoder, size_t offset,
                                 size_t anchor, char *text) {
    size_t length = global_cfg.num_bytes - offset;
    if (offset < anchor && anchor - offset < length)
        length = anchor - offset;
    const binaryInfo *info = decoder->info;
    if (info && info->has_entry && offset < info->entry_offset &&
        info->entry_offset - offset < length)
        length = info->entry_offset - offset;
    /* A section can start inside a broader mapped segment. Resolve each row
       so it gets the tighter boundary and its own runtime address. */
    binaryRegion region;
    uint64_t address = offset;
    if (architecture_region(offset, &region)) {
        size_t within = offset - region.offset;
        address = region.address + within;
        if (region.size - within < length)
            length = region.size - within;
    }
    if (!decoder->enabled || address % decoder->alignment)
        goto invalid;

    if (global_cfg.architecture == ARCH_X86) {
        ZydisDecodedInstruction instruction;
        ZydisDecodedOperand operands[ZYDIS_MAX_OPERAND_COUNT];
        if (!ZYAN_SUCCESS(ZydisDecoderDecodeFull(&decoder->zydis, global_cfg.file + offset,
                                                 length, &instruction, operands)))
            goto invalid;
        if (!ZYAN_SUCCESS(ZydisFormatterFormatInstruction(
                &decoder->formatter, &instruction, operands, instruction.operand_count_visible,
                text, DISASSEMBLED_BUFFER_SIZE, address, ZYAN_NULL)))
            goto invalid;
        return instruction.length;
    }

    const uint8_t *bytes = global_cfg.file + offset;
    size_t available = length;
    if (cs_disasm_iter(decoder->capstone, &bytes, &length, &address, decoder->instruction) &&
        decoder->instruction->size && decoder->instruction->size <= available) {
        snprintf(text, DISASSEMBLED_BUFFER_SIZE, "%s%s%.94s",
                 decoder->instruction->mnemonic, decoder->instruction->op_str[0] ? " " : "",
                 decoder->instruction->op_str);
        return decoder->instruction->size;
    }

invalid:
    snprintf(text, DISASSEMBLED_BUFFER_SIZE, "db %02X", global_cfg.file[offset]);
    return 1;
}

void free_disassembler_buffer(void) {
    free(global_cfg.disassembler_buffer);
    global_cfg.disassembler_buffer = NULL;
}

static void reverse_rows(size_t start, size_t end) {
    while (start < end && start < --end) {
        disassemblerRow temporary = global_cfg.disassembler_buffer[start];
        global_cfg.disassembler_buffer[start++] = global_cfg.disassembler_buffer[end];
        global_cfg.disassembler_buffer[end] = temporary;
    }
}

int disassemble_block(size_t cur_byte) {
    if (!global_cfg.disassembler_buffer || !global_cfg.screenrows)
        return EXIT_SUCCESS;
    if (!global_cfg.file || cur_byte >= global_cfg.num_bytes) {
        memset(global_cfg.disassembler_buffer, 0,
               global_cfg.screenrows * sizeof(disassemblerRow));
        return EXIT_SUCCESS;
    }

    instructionDecoder decoder;
    if (!decoder_init(&decoder))
        die_safely("Failed to initialize disassembler");
    size_t context_rows = global_cfg.screenrows / 2;
    /* Variable-length backward decoding is heuristic. Preserve known entry and
       cached instruction boundaries, and align fixed-width architectures. */
    size_t read_offset = cur_byte > DISASSEMBLY_LOOKBACK
        ? cur_byte - DISASSEMBLY_LOOKBACK : 0;
    size_t maximum = global_cfg.architecture == ARCH_X86
        ? ZYDIS_MAX_INSTRUCTION_LENGTH : MAX_INSTRUCTION_BYTES;
    read_offset = context_rows > read_offset / maximum
        ? 0 : read_offset - context_rows * maximum;
    size_t anchor = global_cfg.num_bytes;
    binaryRegion region;
    if (architecture_region(cur_byte, &region) && read_offset < region.offset)
        read_offset = region.offset;
    size_t alignment = decoder.alignment;
    if (architecture_region(read_offset, &region)) {
        size_t remainder = (region.address % alignment +
                            (read_offset - region.offset) % alignment) % alignment;
        if (remainder <= read_offset - region.offset)
            read_offset -= remainder;
    } else {
        read_offset -= read_offset % alignment;
    }
    const binaryInfo *info = decoder.info;
    if (info && info->has_entry && read_offset <= info->entry_offset &&
        info->entry_offset <= cur_byte)
        anchor = info->entry_offset;
    for (size_t row = 0; row < global_cfg.screenrows; ++row) {
        const disassemblerRow *cached = &global_cfg.disassembler_buffer[row];
        if (cached->start_byte <= cur_byte && read_offset < cached->end_byte &&
            cached->start_byte < cached->end_byte && cached->end_byte <= global_cfg.num_bytes) {
            if (cached->start_byte < anchor)
                anchor = cached->start_byte;
            if (read_offset > anchor)
                read_offset = anchor;
            break;
        }
    }

    size_t history_rows = context_rows + 1;
    size_t history_count = 0;
    size_t history_next = 0;
    memset(global_cfg.disassembler_buffer, 0,
           global_cfg.screenrows * sizeof(disassemblerRow));
    /* Decode once in order: Thumb IT blocks carry decoder state between rows. */
    while (read_offset <= cur_byte) {
        disassemblerRow *row = &global_cfg.disassembler_buffer[history_next];
        row->start_byte = read_offset;
        read_offset += decode_instruction(&decoder, read_offset, anchor, row->diss_str);
        row->end_byte = read_offset;
        history_next = (history_next + 1) % history_rows;
        if (history_count < history_rows)
            history_count++;
    }
    if (history_count == history_rows && history_next) {
        reverse_rows(0, history_next);
        reverse_rows(history_next, history_rows);
        reverse_rows(0, history_rows);
    }

    size_t output_row = history_count;
    while (read_offset < global_cfg.num_bytes && output_row < global_cfg.screenrows) {
        disassemblerRow *row = &global_cfg.disassembler_buffer[output_row++];
        row->start_byte = read_offset;
        read_offset += decode_instruction(&decoder, read_offset, anchor, row->diss_str);
        row->end_byte = read_offset;
    }
    decoder_free(&decoder);
    return EXIT_SUCCESS;
}
