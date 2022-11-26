
#ifndef LHIEW_DISSASEMBLER_H
#define LHIEW_DISSASEMBLER_H
#include "editor.h"
#include <stdio.h>
#include <Zycore/LibC.h>
#include <Zydis/Zydis.h>

int dissameble_block(size_t read_offset_base, size_t buffer_size);

#endif //LHIEW_DISSASEMBLER_H
