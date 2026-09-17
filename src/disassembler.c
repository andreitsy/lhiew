#include "lhiew/types.h"
#include "lhiew/disassembler.h"
#include "lhiew/terminal.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "Zycore/LibC.h"
#include "Zydis/Zydis.h"

#define DISASSEMBLY_LOOKBACK 128

static ZyanStatus decode_instruction(const ZydisDecoder *decoder, size_t offset,
                                     size_t anchor, ZydisDecodedInstruction *instruction,
                                     ZydisDecodedOperand *operands) {
    /* Do not decode across the start of previously displayed instructions. */
    size_t length = offset < anchor ? anchor - offset : global_cfg.num_bytes - offset;
    return ZydisDecoderDecodeFull(decoder, global_cfg.file + offset, length,
                                 instruction, operands);
}

void free_disassembler_buffer(void) {
    free(global_cfg.disassembler_buffer);
    global_cfg.disassembler_buffer = NULL;
}

int disassemble_block(size_t cur_byte) {
    if (!global_cfg.disassembler_buffer || !global_cfg.screenrows) {
        return EXIT_SUCCESS;
    }

    if (!global_cfg.file || cur_byte >= global_cfg.num_bytes) {
        memset(global_cfg.disassembler_buffer, 0,
               global_cfg.screenrows * sizeof(disassemblerRow));
        return EXIT_SUCCESS;
    }

    size_t context_rows = global_cfg.screenrows / 2;
    /* Backward x86 decoding is heuristic. Include enough bytes for full-length
       instructions above the selection, plus the usual alignment lookback. */
    size_t read_offset = cur_byte > DISASSEMBLY_LOOKBACK
        ? cur_byte - DISASSEMBLY_LOOKBACK : 0;
    read_offset = context_rows > read_offset / ZYDIS_MAX_INSTRUCTION_LENGTH
        ? 0 : read_offset - context_rows * ZYDIS_MAX_INSTRUCTION_LENGTH;
    size_t anchor = global_cfg.num_bytes;
    for (size_t row = 0; row < global_cfg.screenrows; ++row) {
        const disassemblerRow *cached = &global_cfg.disassembler_buffer[row];
        if (cached->start_byte <= cur_byte && read_offset < cached->end_byte &&
            cached->start_byte < cached->end_byte &&
            cached->end_byte <= global_cfg.num_bytes) {
            anchor = cached->start_byte;
            if (read_offset > anchor)
                read_offset = anchor;
            break;
        }
    }

    ZydisDecoder decoder;
    if (global_cfg.disassembler_mode == REAL) {
        ZydisDecoderInit(&decoder, ZYDIS_MACHINE_MODE_REAL_16, ZYDIS_STACK_WIDTH_16);
    } else if (global_cfg.disassembler_mode == MODE_LONG_COMPAT_16) {
        ZydisDecoderInit(&decoder, ZYDIS_MACHINE_MODE_LONG_COMPAT_16, ZYDIS_STACK_WIDTH_16);
    } else if (global_cfg.disassembler_mode == MODE_LONG_COMPAT_32) {
        ZydisDecoderInit(&decoder, ZYDIS_MACHINE_MODE_LONG_COMPAT_32, ZYDIS_STACK_WIDTH_32);
    } else if (global_cfg.disassembler_mode == MODE_LONG_COMPAT_64) {
        ZydisDecoderInit(&decoder, ZYDIS_MACHINE_MODE_LONG_64, ZYDIS_STACK_WIDTH_64);
    } else {
        die_safely("disassemble_block non-existing mode!");
        return EXIT_FAILURE;
    }

    /* Use the row buffer as a ring of instruction offsets while finding the
       selection. Keep only the preceding half-screen and the selected row. */
    size_t history_rows = context_rows + 1;
    size_t history_count = 0;
    size_t history_next = 0;
    while (read_offset <= cur_byte) {
        ZydisDecodedInstruction instruction;
        ZydisDecodedOperand operands[ZYDIS_MAX_OPERAND_COUNT];
        ZyanStatus status = decode_instruction(&decoder, read_offset, anchor,
                                               &instruction, operands);
        global_cfg.disassembler_buffer[history_next].start_byte = read_offset;
        history_next = (history_next + 1) % history_rows;
        if (history_count < history_rows)
            history_count++;
        read_offset += ZYAN_SUCCESS(status) ? instruction.length : 1;
    }
    read_offset = global_cfg.disassembler_buffer[
        history_count == history_rows ? history_next : 0].start_byte;
    memset(global_cfg.disassembler_buffer, 0,
           global_cfg.screenrows * sizeof(disassemblerRow));

    ZydisFormatter formatter;
    if (!ZYAN_SUCCESS(ZydisFormatterInit(&formatter, ZYDIS_FORMATTER_STYLE_ATT)) ||
        !ZYAN_SUCCESS(ZydisFormatterSetProperty(&formatter,
                                                ZYDIS_FORMATTER_PROP_FORCE_SEGMENT, ZYAN_FALSE)) ||
        !ZYAN_SUCCESS(ZydisFormatterSetProperty(&formatter,
                                                ZYDIS_FORMATTER_PROP_FORCE_SIZE, ZYAN_FALSE))) {
        die_safely("Failed to initialize instruction-formatter\n");
        return EXIT_FAILURE;
    }

    size_t output_row = 0;
    while (read_offset < global_cfg.num_bytes && output_row < global_cfg.screenrows) {
        ZydisDecodedInstruction instruction;
        ZydisDecodedOperand operands[ZYDIS_MAX_OPERAND_COUNT];
        char format_buffer[DISASSEMBLED_BUFFER_SIZE];
        ZyanStatus status = decode_instruction(&decoder, read_offset, anchor,
                                               &instruction, operands);
        size_t instruction_length = 1;

        if (ZYAN_SUCCESS(status)) {
            status = ZydisFormatterFormatInstruction(
                &formatter, &instruction, operands, instruction.operand_count_visible,
                format_buffer, sizeof(format_buffer), read_offset, ZYAN_NULL);
            if (ZYAN_SUCCESS(status)) {
                instruction_length = instruction.length;
            }
        }
        if (!ZYAN_SUCCESS(status)) {
            /* Invalid or incomplete instructions still consume one file byte. */
            snprintf(format_buffer, sizeof(format_buffer), "db %02X",
                     global_cfg.file[read_offset]);
        }

        size_t end_byte = read_offset + instruction_length;
        disassemblerRow *row = &global_cfg.disassembler_buffer[output_row++];
        row->start_byte = read_offset;
        row->end_byte = end_byte;
        strcpy(row->diss_str, format_buffer);
        read_offset = end_byte;
    }

    return EXIT_SUCCESS;
}
