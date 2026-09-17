#include "lhiew/types.h"
#include "lhiew/disassembler.h"
#include "lhiew/terminal.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "Zycore/LibC.h"
#include "Zydis/Zydis.h"

#define DISASSEMBLY_LOOKBACK 128

void free_disassembler_buffer(void) {
    free(global_cfg.disassembler_buffer);
    global_cfg.disassembler_buffer = NULL;
}

int disassemble_block(size_t cur_byte) {
    if (!global_cfg.disassembler_buffer || !global_cfg.screenrows) {
        return EXIT_SUCCESS;
    }

    size_t read_offset = cur_byte > DISASSEMBLY_LOOKBACK
        ? cur_byte - DISASSEMBLY_LOOKBACK : 0;
    for (size_t row = 0; row < global_cfg.screenrows; ++row) {
        const disassemblerRow *cached = &global_cfg.disassembler_buffer[row];
        if (cached->start_byte <= cur_byte && cur_byte < cached->end_byte &&
            cached->end_byte <= global_cfg.num_bytes) {
            read_offset = cached->start_byte;
            break;
        }
    }
    memset(global_cfg.disassembler_buffer, 0,
           global_cfg.screenrows * sizeof(disassemblerRow));
    if (!global_cfg.file || cur_byte >= global_cfg.num_bytes) {
        return EXIT_SUCCESS;
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
        ZyanStatus status = ZydisDecoderDecodeFull(
            &decoder, global_cfg.file + read_offset,
            global_cfg.num_bytes - read_offset, &instruction, operands);
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
        if (cur_byte < end_byte) {
            disassemblerRow *row = &global_cfg.disassembler_buffer[output_row++];
            row->start_byte = read_offset;
            row->end_byte = end_byte;
            strcpy(row->diss_str, format_buffer);
        }
        read_offset = end_byte;
    }

    return EXIT_SUCCESS;
}
