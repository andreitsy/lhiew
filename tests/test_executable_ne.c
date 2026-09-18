#include "lhiew/executable.h"
#include "test_harness.h"

enum { NE_HEADER = 0x80, NE_SEGMENT = 0xc0, NE_MODULES = 0xc8,
       NE_NAMES = 0xd0, NE_CODE = 0x200, NE_RELOCS = 0x212 };

static void put16(uint8_t *p, uint16_t value) {
    p[0] = (uint8_t)value;
    p[1] = (uint8_t)(value >> 8);
}

static void put32(uint8_t *p, uint32_t value) {
    put16(p, (uint16_t)value);
    put16(p + 2, (uint16_t)(value >> 16));
}

/* Serialized Windows NE: two libraries, one named and one ordinal import.
 * Layout follows Open Watcom's exeos2.h and Wine's NE relocation reader. */
static void make_ne(uint8_t data[1024]) {
    memset(data, 0, 1024);
    memcpy(data, "MZ", 2);
    put32(data + 0x3c, NE_HEADER);
    memcpy(data + NE_HEADER, "NE", 2);
    put16(data + NE_HEADER + 4, 0x70);
    put16(data + NE_HEADER + 6, 1);
    put16(data + NE_HEADER + 28, 1);
    put16(data + NE_HEADER + 30, 2);
    put16(data + NE_HEADER + 34, 0x40);
    put16(data + NE_HEADER + 40, 0x48);
    put16(data + NE_HEADER + 42, 0x50);
    put16(data + NE_HEADER + 50, 4);
    data[NE_HEADER + 54] = 2;
    put16(data + NE_SEGMENT, 0x20);
    put16(data + NE_SEGMENT + 2, 0x10);
    put16(data + NE_SEGMENT + 4, 0x100);
    put16(data + NE_SEGMENT + 6, 0x10);
    put16(data + NE_MODULES, 0);
    put16(data + NE_MODULES + 2, 7);
    memcpy(data + NE_NAMES, "\x06" "KERNEL" "\x04" "USER" "\x09" "FatalExit", 22);
    put16(data + NE_CODE, 0xffff);
    put16(data + NE_CODE + 4, 0xffff);
    put16(data + NE_CODE + 0x10, 2);
    const uint8_t relocations[] = {3, 2, 0, 0, 1, 0, 12, 0,
                                  3, 1, 4, 0, 2, 0, 123, 0};
    memcpy(data + NE_RELOCS, relocations, sizeof(relocations));
}

static executableRow *find_row(executableInfo *info, executableRowKind kind, size_t index) {
    for (size_t i = 0; i < info->count; ++i) {
        if (info->rows[i].kind == kind && index-- == 0) return info->rows + i;
    }
    return NULL;
}

static void test_ne_name_and_ordinal_imports(void) {
    uint8_t data[1024];
    make_ne(data);
    executableInfo info = {0};
    executable_parse(data, sizeof(data), &info);
    ASSERT_EQ(info.status, EXE_OK);
    ASSERT_STR_EQ(info.format, "NE");
    executableRow *header = find_row(&info, EXE_HEADER, 0);
    executableRow *module = find_row(&info, EXE_MODULE, 0);
    executableRow *region = find_row(&info, EXE_REGION, 0);
    executableRow *named = find_row(&info, EXE_IMPORT, 0);
    executableRow *ordinal = find_row(&info, EXE_IMPORT, 1);
    ASSERT(header && module && region && named && ordinal);
    ASSERT_EQ(header->offset, (size_t)NE_HEADER);
    ASSERT_EQ(header->length, (size_t)64);
    ASSERT_EQ(module->offset, (size_t)NE_MODULES);
    ASSERT_EQ(module->name_offset, (size_t)NE_NAMES + 1);
    ASSERT_EQ(module->name_length, (size_t)6);
    ASSERT_EQ(region->offset, (size_t)NE_CODE);
    ASSERT_EQ(region->length, (size_t)0x10);
    ASSERT_STR_EQ(named->label, "KERNEL!FatalExit [segment 1]");
    ASSERT_EQ(named->offset, (size_t)NE_RELOCS);
    ASSERT_EQ(named->name_offset, (size_t)NE_NAMES + 13);
    ASSERT_EQ(named->name_length, (size_t)9);
    ASSERT_EQ(named->ordinal_offset, SIZE_MAX);
    ASSERT(named->editable);
    ASSERT_STR_EQ(ordinal->label, "USER!#123 [segment 1]");
    ASSERT_EQ(ordinal->name_offset, SIZE_MAX);
    ASSERT_EQ(ordinal->ordinal_offset, (size_t)NE_RELOCS + 14);
    ASSERT_EQ(ordinal->ordinal_width, 2u);
    ASSERT_EQ(ordinal->ordinal, 123u);
    ASSERT_EQ(ordinal->ordinal_max, UINT16_MAX);
    ASSERT(ordinal->editable);
    executable_free(&info);
}

static void test_ne_reparse_shared_names_and_ordinal(void) {
    uint8_t data[1024];
    make_ne(data);
    put16(data + NE_CODE + 0x10, 3);
    memcpy(data + NE_RELOCS + 16, data + NE_RELOCS, 8);
    executableInfo info = {0};
    executable_parse(data, sizeof(data), &info);
    ASSERT_EQ(info.status, EXE_OK);
    executableRow *first = find_row(&info, EXE_IMPORT, 0);
    executableRow *shared = find_row(&info, EXE_IMPORT, 2);
    ASSERT(first && shared);
    ASSERT_EQ(first->name_offset, shared->name_offset);
    memcpy(data + first->name_offset, "OtherExit", first->name_length);
    memcpy(data + NE_NAMES + 1, "SYSTEM", 6);
    put16(data + NE_RELOCS + 14, 456);
    executable_parse(data, sizeof(data), &info);
    ASSERT_EQ(info.status, EXE_OK);
    first = find_row(&info, EXE_IMPORT, 0);
    shared = find_row(&info, EXE_IMPORT, 2);
    executableRow *ordinal = find_row(&info, EXE_IMPORT, 1);
    ASSERT(first && shared && ordinal);
    ASSERT_STR_EQ(first->label, "SYSTEM!OtherExit [segment 1]");
    ASSERT_STR_EQ(shared->label, "SYSTEM!OtherExit [segment 1]");
    ASSERT_STR_EQ(ordinal->label, "USER!#456 [segment 1]");
    executable_free(&info);
}

static void test_ne_rejects_invalid_module_indices(void) {
    const uint16_t indices[] = {0, 3, UINT16_MAX};
    for (size_t i = 0; i < sizeof(indices) / sizeof(indices[0]); ++i) {
        uint8_t data[1024];
        make_ne(data);
        put16(data + NE_RELOCS + 4, indices[i]);
        executableInfo info = {0};
        executable_parse(data, sizeof(data), &info);
        ASSERT_EQ(info.status, EXE_MALFORMED);
        executable_free(&info);
    }
}

static void test_ne_rejects_invalid_name_references(void) {
    const uint16_t offsets[] = {1, 31, 32, UINT16_MAX};
    for (size_t i = 0; i < sizeof(offsets) / sizeof(offsets[0]); ++i) {
        uint8_t data[1024];
        make_ne(data);
        put16(data + NE_RELOCS + 6, offsets[i]);
        executableInfo info = {0};
        executable_parse(data, sizeof(data), &info);
        ASSERT_EQ(info.status, EXE_MALFORMED);
        executable_free(&info);
    }
}

static void test_ne_rejects_truncated_names_and_metadata(void) {
    uint8_t data[1024];
    make_ne(data);
    data[NE_NAMES + 12] = 255;
    executableInfo info = {0};
    executable_parse(data, sizeof(data), &info);
    ASSERT_EQ(info.status, EXE_MALFORMED);
    make_ne(data);
    put16(data + NE_HEADER + 42, 0x49);
    executable_parse(data, sizeof(data), &info);
    ASSERT_EQ(info.status, EXE_MALFORMED);
    make_ne(data);
    put16(data + NE_HEADER + 28, 2);
    executable_parse(data, sizeof(data), &info);
    ASSERT_EQ(info.status, EXE_MALFORMED);
    make_ne(data);
    executable_parse(data, NE_HEADER + 63, &info);
    ASSERT_EQ(info.status, EXE_MALFORMED);
    executable_free(&info);
}

static void test_ne_rejects_truncated_relocations(void) {
    uint8_t data[1024];
    make_ne(data);
    executableInfo info = {0};
    executable_parse(data, NE_RELOCS + 15, &info);
    ASSERT_EQ(info.status, EXE_MALFORMED);
    executable_parse(data, NE_RELOCS - 1, &info);
    ASSERT_EQ(info.status, EXE_MALFORMED);
    put16(data + NE_CODE + 0x10, UINT16_MAX);
    executable_parse(data, sizeof(data), &info);
    ASSERT_EQ(info.status, EXE_MALFORMED);
    executable_free(&info);
}

static void test_ne_rejects_bad_segment_spans(void) {
    uint8_t data[1024];
    make_ne(data);
    executableInfo info = {0};
    put16(data + NE_HEADER + 50, UINT16_MAX);
    executable_parse(data, sizeof(data), &info);
    ASSERT_EQ(info.status, EXE_MALFORMED);
    make_ne(data);
    put16(data + NE_SEGMENT, 0);
    executable_parse(data, sizeof(data), &info);
    ASSERT_EQ(info.status, EXE_MALFORMED);
    make_ne(data);
    put16(data + NE_SEGMENT, 0xd);
    executable_parse(data, sizeof(data), &info);
    ASSERT_EQ(info.status, EXE_MALFORMED);
    make_ne(data);
    put16(data + NE_SEGMENT + 2, 0);
    executable_parse(data, sizeof(data), &info);
    ASSERT_EQ(info.status, EXE_MALFORMED);
    executable_free(&info);
}

static void test_ne_no_imports_and_zero_fill_segment(void) {
    uint8_t data[1024];
    make_ne(data);
    put16(data + NE_HEADER + 30, 0);
    put16(data + NE_SEGMENT, 0);
    put16(data + NE_SEGMENT + 4, 1);
    executableInfo info = {0};
    executable_parse(data, sizeof(data), &info);
    ASSERT_EQ(info.status, EXE_OK);
    ASSERT(!find_row(&info, EXE_MODULE, 0));
    ASSERT(!find_row(&info, EXE_IMPORT, 0));
    executableRow *region = find_row(&info, EXE_REGION, 0);
    ASSERT_NE(region, NULL);
    ASSERT_EQ(region->offset, (size_t)NE_SEGMENT);
    executable_free(&info);
}

static void test_ne_rejects_overlapping_segment_and_fixup_data(void) {
    for (int alias_fixup = 0; alias_fixup < 2; ++alias_fixup) {
        for (int reversed = 0; reversed < 2; ++reversed) {
            uint8_t data[1024];
            make_ne(data);
            /* Make space for a second segment record before module references. */
            memmove(data + 0xd4, data + NE_NAMES, 22);
            put16(data + NE_HEADER + 28, 2);
            put16(data + NE_HEADER + 40, 0x50);
            put16(data + NE_HEADER + 42, 0x54);
            put16(data + 0xd0, 0);
            put16(data + 0xd2, 7);
            /* Segment 2 aliases either segment 1 code or its import ordinal. */
            put16(data + NE_SEGMENT + 8, alias_fixup ? 0x22 : 0x20);
            put16(data + NE_SEGMENT + 10, 2);
            put16(data + NE_SEGMENT + 12, 0);
            put16(data + NE_SEGMENT + 14, 2);
            if (reversed) {
                uint8_t temporary[8];
                memcpy(temporary, data + NE_SEGMENT, 8);
                memcpy(data + NE_SEGMENT, data + NE_SEGMENT + 8, 8);
                memcpy(data + NE_SEGMENT + 8, temporary, 8);
            }
            executableInfo info = {0};
            executable_parse(data, sizeof(data), &info);
            ASSERT_EQ(info.status, EXE_MALFORMED);
            for (size_t i = 0; i < info.count; ++i) ASSERT_EQ(info.rows[i].editable, 0);
            executable_free(&info);
        }
    }
}

static void test_ne_skips_internal_relocations(void) {
    uint8_t data[1024];
    make_ne(data);
    data[NE_RELOCS + 1] = 0;
    data[NE_RELOCS + 9] = 3;
    executableInfo info = {0};
    executable_parse(data, sizeof(data), &info);
    ASSERT_EQ(info.status, EXE_OK);
    ASSERT(!find_row(&info, EXE_IMPORT, 0));
    executable_free(&info);
}

static void test_ne_accepts_full_length_names(void) {
    uint8_t data[2048] = {0};
    make_ne(data);
    memcpy(data + 0x400, data + NE_CODE, 0x22);
    put16(data + NE_SEGMENT, 0x40);
    put16(data + NE_MODULES + 2, 256);
    data[NE_NAMES] = 255;
    memset(data + NE_NAMES + 1, 'M', 255);
    data[NE_NAMES + 256] = 4;
    memcpy(data + NE_NAMES + 257, "USER", 4);
    data[NE_NAMES + 261] = 255;
    memset(data + NE_NAMES + 262, 'F', 255);
    put16(data + NE_HEADER + 4, NE_NAMES + 517 - NE_HEADER);
    data[NE_NAMES + 517] = 0;
    put16(data + 0x418, 261);
    executableInfo info = {0};
    executable_parse(data, sizeof(data), &info);
    ASSERT_EQ(info.status, EXE_OK);
    executableRow *module = find_row(&info, EXE_MODULE, 0);
    executableRow *import = find_row(&info, EXE_IMPORT, 0);
    ASSERT(module && import);
    ASSERT_EQ(module->name_length, (size_t)255);
    ASSERT_EQ(import->name_length, (size_t)255);
    ASSERT_EQ(import->name_offset, (size_t)NE_NAMES + 262);
    ASSERT_EQ(strlen(import->label), (size_t)523);
    executable_free(&info);
}

static void test_ne_zero_length_means_64k_segment(void) {
    size_t size = NE_CODE + 65536 + 18;
    uint8_t *data = calloc(size, 1);
    ASSERT(data);
    make_ne(data);
    memcpy(data + NE_CODE + 65536, data + NE_CODE + 16, 18);
    put16(data + NE_SEGMENT + 2, 0);
    executableInfo info = {0};
    executable_parse(data, size, &info);
    ASSERT_EQ(info.status, EXE_OK);
    executableRow *region = find_row(&info, EXE_REGION, 0);
    executableRow *import = find_row(&info, EXE_IMPORT, 0);
    ASSERT(region && import);
    ASSERT_EQ(region->length, (size_t)65536);
    ASSERT_EQ(import->offset, (size_t)NE_CODE + 65536 + 2);
    executable_free(&info);
    free(data);
}

static void test_ne_iterated_and_additive_fixups(void) {
    uint8_t data[1024];
    make_ne(data);
    put16(data + NE_SEGMENT + 4, 0x108);
    /* Encoded segment data; import record offsets remain physical. */
    put16(data + NE_CODE, 4);
    put16(data + NE_CODE + 2, 12);
    memset(data + NE_CODE + 4, 0, 12);
    data[NE_RELOCS + 1] |= 4;
    executableInfo info = {0};
    executable_parse(data, sizeof(data), &info);
    ASSERT_EQ(info.status, EXE_OK);
    executableRow *region = find_row(&info, EXE_REGION, 0);
    executableRow *import = find_row(&info, EXE_IMPORT, 0);
    ASSERT(region && import);
    ASSERT(strstr(region->label, "iterated"));
    ASSERT_EQ(import->offset, (size_t)NE_RELOCS);
    ASSERT_STR_EQ(import->label, "KERNEL!FatalExit [segment 1]");
    executable_free(&info);
}

static void test_ne_self_loading_is_explicitly_unsupported(void) {
    uint8_t data[1024];
    make_ne(data);
    put16(data + NE_HEADER + 12, 0x0800);
    executableInfo info = {0};
    executable_parse(data, sizeof(data), &info);
    ASSERT_EQ(info.status, EXE_UNSUPPORTED);
    ASSERT(strstr(info.message, "Self-loading"));
    /* The same flag identifies bound-family API for OS/2, not a loader. */
    data[NE_HEADER + 54] = 1;
    executable_parse(data, sizeof(data), &info);
    ASSERT_EQ(info.status, EXE_OK);
    executable_free(&info);
}

int main(void) {
    RUN_TEST(test_ne_name_and_ordinal_imports);
    RUN_TEST(test_ne_reparse_shared_names_and_ordinal);
    RUN_TEST(test_ne_rejects_invalid_module_indices);
    RUN_TEST(test_ne_rejects_invalid_name_references);
    RUN_TEST(test_ne_rejects_truncated_names_and_metadata);
    RUN_TEST(test_ne_rejects_truncated_relocations);
    RUN_TEST(test_ne_rejects_bad_segment_spans);
    RUN_TEST(test_ne_no_imports_and_zero_fill_segment);
    RUN_TEST(test_ne_rejects_overlapping_segment_and_fixup_data);
    RUN_TEST(test_ne_skips_internal_relocations);
    RUN_TEST(test_ne_accepts_full_length_names);
    RUN_TEST(test_ne_zero_length_means_64k_segment);
    RUN_TEST(test_ne_iterated_and_additive_fixups);
    RUN_TEST(test_ne_self_loading_is_explicitly_unsupported);
    TEST_REPORT();
}
