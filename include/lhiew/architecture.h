#pragma once

#include "lhiew/types.h"
#include "lhiew/binary.h"

typedef struct architectureProfile {
    const char *name;
    architectureSpec spec;
} architectureProfile;

size_t architecture_profile_count(void);
const architectureProfile *architecture_profile(size_t index);
size_t architecture_current_profile(void);
const char *architecture_current_name(void);
const char *architecture_detection_label(void);
void architecture_select(size_t index);
void architecture_detect_file(void);
void architecture_jump_to_entry(void);
const binaryInfo *architecture_binary_info(void);
int architecture_region(size_t offset, binaryRegion *region);
size_t architecture_alignment(void);
