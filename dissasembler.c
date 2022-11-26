#include "dissasembler.h"


int dissameble_block(size_t read_offset_base, size_t buffer_size) {
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
    if (!ZYAN_SUCCESS(ZydisFormatterInit(&formatter, ZYDIS_FORMATTER_STYLE_INTEL)) ||
        !ZYAN_SUCCESS(ZydisFormatterSetProperty(&formatter,
                                                ZYDIS_FORMATTER_PROP_FORCE_SEGMENT, ZYAN_TRUE)) ||
        !ZYAN_SUCCESS(ZydisFormatterSetProperty(&formatter,
                                                ZYDIS_FORMATTER_PROP_FORCE_SIZE, ZYAN_TRUE))) {
        die_safely("Failed to initialized instruction-formatter\n");
        return EXIT_FAILURE;
    }

    ZyanUSize buffer_remaining = 0;
    uint8_t buffer_block[buffer_size];
    memcpy(buffer_block, &global_cfg.file[read_offset_base], buffer_size);
    size_t output_row = 0;
    do {
        ZydisDecodedInstruction instruction;
        ZydisDecodedOperand operands[ZYDIS_MAX_OPERAND_COUNT];
        ZyanStatus status;
        ZyanUSize read_offset = 0;
        while ((status = ZydisDecoderDecodeFull(
                &decoder, buffer_block + read_offset,
                buffer_size - read_offset,
                &instruction, operands)) != ZYDIS_STATUS_NO_MORE_DATA
                && output_row < global_cfg.screenrows) {
            const ZyanU64 runtime_address = read_offset_base + read_offset;
            char format_buffer[DISSASMBLED_BUFFER_SIZE];

            char buf_code[128] = "";
            char* buf = buf_code;
            char* end_of_buf = buf + sizeof(buf_code);
            for (size_t i = 0; i < instruction.length; i++)
            {
                if (buf_code + 3 < end_of_buf)
                {
                    buf += sprintf(buf, "%02x",
                                   buffer_block[runtime_address + i - read_offset_base]);
                }
            }

            if (!ZYAN_SUCCESS(status)) {
                sprintf(global_cfg.dissasembled_buffer[output_row],
                        "%08lx| %-20s | db %02X",
                        runtime_address, buf_code,
                        buffer_block[read_offset++]);
//                sprintf(global_cfg.dissasembled_buffer[output_row++],
//                        "db %02X",
//                        buffer_block[read_offset++]);
                continue;
            }

            ZydisFormatterFormatInstruction(&formatter, &instruction, operands,
                                            instruction.operand_count_visible,
                                            format_buffer, sizeof(format_buffer),
                                            runtime_address, ZYAN_NULL);
            sprintf(global_cfg.dissasembled_buffer[output_row],
                    "%08lx |  %-20s | %s", runtime_address, buf_code,
                    format_buffer);
            //strcpy(global_cfg.dissasembled_buffer[output_row], format_buffer);
            read_offset += instruction.length;
            output_row++;
        }

        buffer_remaining = 0;
        if (read_offset < sizeof(buffer_block)) {
            buffer_remaining = sizeof(buffer_block) - read_offset;
            memmove(buffer_block, buffer_block + read_offset, buffer_remaining);
        }
        read_offset_base += read_offset;

    } while (read_offset_base <= buffer_size && output_row < global_cfg.screenrows);

    return EXIT_SUCCESS;
}
