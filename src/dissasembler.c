#include "dissasembler.h"
#include <stdbool.h>
#define MAX_OP_SIZE 128

int dissameble_block(size_t cur_byte) {
    ZydisDecoder decoder;
    if (global_cfg.dissasembler_mode == REAL) {
        ZydisDecoderInit(&decoder, ZYDIS_MACHINE_MODE_REAL_16, ZYDIS_STACK_WIDTH_16);
    } else if (global_cfg.dissasembler_mode == MODE_LONG_COMPAT_16) {
        ZydisDecoderInit(&decoder, ZYDIS_MACHINE_MODE_LONG_COMPAT_16, ZYDIS_STACK_WIDTH_16);
    } else if (global_cfg.dissasembler_mode == MODE_LONG_COMPAT_32) {
        ZydisDecoderInit(&decoder, ZYDIS_MACHINE_MODE_LONG_COMPAT_32, ZYDIS_STACK_WIDTH_32);
    } else if (global_cfg.dissasembler_mode == MODE_LONG_COMPAT_64) {
        ZydisDecoderInit(&decoder, ZYDIS_MACHINE_MODE_LONG_64, ZYDIS_STACK_WIDTH_64);
    } else {
        die_safely("dissameble_block non-existing mode!");
        return EXIT_FAILURE;
    }

    ZydisFormatter formatter;
    if (!ZYAN_SUCCESS(ZydisFormatterInit(&formatter, ZYDIS_FORMATTER_STYLE_ATT)) ||
        !ZYAN_SUCCESS(ZydisFormatterSetProperty(&formatter,
                                                ZYDIS_FORMATTER_PROP_FORCE_SEGMENT, ZYAN_FALSE)) ||
        !ZYAN_SUCCESS(ZydisFormatterSetProperty(&formatter,
                                                ZYDIS_FORMATTER_PROP_FORCE_SIZE, ZYAN_FALSE))) {
        die_safely("Failed to initialized instruction-formatter\n");
        return EXIT_FAILURE;
    }

    size_t buffer_size = global_cfg.screenrows * MAX_OP_SIZE;
    ZyanUSize buffer_remaining = 0;
    uint8_t buffer_block[buffer_size];
    size_t read_offset_base = 0;
    if (cur_byte) {
        for(size_t row; row < global_cfg.screenrows;++row) {
            if (global_cfg.dissasembler_buffer[row].start_byte <= cur_byte
                && cur_byte < global_cfg.dissasembler_buffer[row].end_byte) {
                read_offset_base = global_cfg.dissasembler_buffer[row].start_byte;
                break;
            }
        }
        if (read_offset_base == 0) {
            read_offset_base = cur_byte;
            if (read_offset_base > MAX_OP_SIZE) {
                read_offset_base -= MAX_OP_SIZE;
            } else {
                read_offset_base = 0;
            }
        }
    }

    if (read_offset_base + buffer_size > global_cfg.num_bytes) {
        buffer_size = global_cfg.num_bytes - read_offset_base - buffer_size;
    }
    memcpy(buffer_block, &global_cfg.file[read_offset_base], buffer_size);
    size_t output_row = 0;
    bool runtime_after_cur_byte = false;
    do {
        ZydisDecodedInstruction instruction;
        ZydisDecodedOperand operands[ZYDIS_MAX_OPERAND_COUNT];
        ZyanStatus status;
        ZyanUSize read_offset = 0;
        while ((status = ZydisDecoderDecodeFull(
                &decoder, buffer_block + read_offset,
                buffer_size - read_offset,
                &instruction, operands)) != ZYDIS_STATUS_NO_MORE_DATA &&
               output_row < global_cfg.screenrows) {
            const ZyanU64 runtime_address = read_offset_base + read_offset;
            char format_buffer[DISSASMBLED_BUFFER_SIZE];

            if (!ZYAN_SUCCESS(status)) {
                if (runtime_after_cur_byte || (runtime_address >= cur_byte)) {
                    global_cfg.dissasembler_buffer[output_row].start_byte = runtime_address;
                    global_cfg.dissasembler_buffer[output_row].end_byte = runtime_address + 1;
                    sprintf(global_cfg.dissasembler_buffer[output_row].diss_str,
                            "db %02X", buffer_block[read_offset]);
                    output_row++;
                    runtime_after_cur_byte = true;
                }
                read_offset++;
                continue;
            }

            ZydisFormatterFormatInstruction(&formatter, &instruction, operands,
                                            instruction.operand_count_visible,
                                            format_buffer, sizeof(format_buffer),
                                            runtime_address, ZYAN_NULL);
            if (runtime_after_cur_byte
                || (runtime_address >= cur_byte || cur_byte < runtime_address + instruction.length)) {
                global_cfg.dissasembler_buffer[output_row].start_byte = runtime_address;
                global_cfg.dissasembler_buffer[output_row].end_byte = runtime_address + instruction.length;
                strcpy(global_cfg.dissasembler_buffer[output_row].diss_str, format_buffer);
                output_row++;
                runtime_after_cur_byte = true;
            }
            read_offset += instruction.length;
        }

        buffer_remaining = 0;
        if (read_offset < sizeof(buffer_block)) {
            buffer_remaining = sizeof(buffer_block) - read_offset;
            memmove(buffer_block, buffer_block + read_offset, buffer_remaining);
        }
        read_offset_base += read_offset;
    } while (read_offset_base <= buffer_size &&
             output_row < global_cfg.screenrows);

    return EXIT_SUCCESS;
}
