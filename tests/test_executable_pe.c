#include "lhiew/executable.h"
#include "test_harness.h"

enum { HEADER = 0x80, OPTIONAL = 0x98, RAW = 0x200,
       LOOKUP = 0x240, IAT = 0x280, MODULE = 0x300, FUNCTION = 0x320 };

static void put16(uint8_t *p, uint16_t value) {
    p[0] = (uint8_t)value;
    p[1] = (uint8_t)(value >> 8);
}

static void put32(uint8_t *p, uint32_t value) {
    put16(p, (uint16_t)value);
    put16(p + 2, (uint16_t)(value >> 16));
}

static void put_thunk(uint8_t *p, uint64_t value, int wide) {
    put32(p, (uint32_t)value);
    if (wide) put32(p + 4, (uint32_t)(value >> 32));
}

static size_t directories_offset(int wide) {
    return OPTIONAL + (wide ? 112 : 96);
}

static size_t sections_offset(int wide) {
    return OPTIONAL + (wide ? 240 : 224);
}

/* Serialized PE32/PE32+: .idata with named and ordinal imports.
 * https://learn.microsoft.com/en-us/windows/win32/debug/pe-format */
static void make_pe(uint8_t data[2048], int wide) {
    memset(data, 0, 2048);
    memcpy(data, "MZ", 2);
    put32(data + 0x3c, HEADER);
    memcpy(data + HEADER, "PE\0\0", 4);
    put16(data + HEADER + 4, wide ? 0x8664 : 0x14c);
    put16(data + HEADER + 6, 1);
    put16(data + HEADER + 20, wide ? 240 : 224);
    put16(data + OPTIONAL, wide ? 0x20b : 0x10b);
    put32(data + OPTIONAL + 16, 0x1000);
    put32(data + OPTIONAL + 60, RAW);
    size_t directories = directories_offset(wide);
    put32(data + directories - 4, 16);
    put32(data + directories + 8, 0x1000);
    put32(data + directories + 12, 40);
    size_t section = sections_offset(wide);
    memcpy(data + section, ".idata", 6);
    put32(data + section + 8, 0x400);
    put32(data + section + 12, 0x1000);
    put32(data + section + 16, 0x400);
    put32(data + section + 20, RAW);
    put32(data + section + 36, 0xc0000040);
    put32(data + RAW, 0x1040);
    put32(data + RAW + 12, 0x1100);
    put32(data + RAW + 16, 0x1080);
    size_t width = wide ? 8 : 4;
    uint64_t ordinal = (UINT64_C(1) << (width * 8 - 1)) | 123;
    put_thunk(data + LOOKUP, 0x1120, wide);
    put_thunk(data + LOOKUP + width, ordinal, wide);
    memcpy(data + IAT, data + LOOKUP, width * 3);
    memcpy(data + MODULE, "KERNEL32.dll", 13);
    put16(data + FUNCTION, 42);
    memcpy(data + FUNCTION + 2, "ExitProcess", 12);
}

static executableRow *find_row(executableInfo *info, executableRowKind kind, size_t index) {
    for (size_t i = 0; i < info->count; ++i) {
        if (info->rows[i].kind == kind && index-- == 0) return info->rows + i;
    }
    return NULL;
}

static void test_pe32_and_pe64_name_and_ordinal_imports(void) {
    for (int wide = 0; wide <= 1; ++wide) {
        uint8_t data[2048];
        make_pe(data, wide);
        executableInfo info = {0};
        executable_parse(data, sizeof(data), &info);
        ASSERT_EQ(info.status, EXE_OK);
        ASSERT_STR_EQ(info.format, wide ? "PE32+" : "PE32");
        executableRow *header = find_row(&info, EXE_HEADER, 0);
        executableRow *region = find_row(&info, EXE_REGION, 0);
        executableRow *module = find_row(&info, EXE_MODULE, 0);
        executableRow *named = find_row(&info, EXE_IMPORT, 0);
        executableRow *ordinal = find_row(&info, EXE_IMPORT, 1);
        ASSERT(header && region && module && named && ordinal);
        ASSERT_EQ(header->offset, (size_t)HEADER);
        ASSERT_EQ(region->offset, (size_t)RAW);
        ASSERT_EQ(region->length, (size_t)0x400);
        ASSERT_EQ(module->offset, (size_t)RAW);
        ASSERT_EQ(module->name_offset, (size_t)MODULE);
        ASSERT_EQ(module->name_length, (size_t)12);
        ASSERT(module->editable);
        ASSERT_STR_EQ(named->label, "KERNEL32.dll!ExitProcess");
        ASSERT_EQ(named->offset, (size_t)LOOKUP);
        ASSERT_EQ(named->name_offset, (size_t)FUNCTION + 2);
        ASSERT_EQ(named->name_length, (size_t)11);
        ASSERT_EQ(named->ordinal_offset, SIZE_MAX);
        ASSERT(named->editable);
        ASSERT_STR_EQ(ordinal->label, "KERNEL32.dll!#123");
        ASSERT_EQ(ordinal->ordinal_width, wide ? 8u : 4u);
        ASSERT_EQ(ordinal->ordinal_offset, (size_t)LOOKUP + ordinal->ordinal_width);
        ASSERT_EQ(ordinal->mirror_offset, (size_t)IAT + ordinal->ordinal_width);
        ASSERT_EQ(ordinal->ordinal, 123u);
        ASSERT_EQ(ordinal->ordinal_max, 65535u);
        ASSERT_EQ(ordinal->ordinal_flags, UINT64_C(1) << (ordinal->ordinal_width * 8 - 1));
        ASSERT_EQ(ordinal->name_offset, SIZE_MAX);
        ASSERT(ordinal->editable);
        executable_free(&info);
    }
}

static void test_pe_reparse_changed_name_and_mirrored_ordinal(void) {
    for (int wide = 0; wide <= 1; ++wide) {
        uint8_t data[2048];
        make_pe(data, wide);
        executableInfo info = {0};
        executable_parse(data, sizeof(data), &info);
        executableRow *ordinal = find_row(&info, EXE_IMPORT, 1);
        ASSERT(ordinal);
        put_thunk(data + ordinal->ordinal_offset, ordinal->ordinal_flags | 456, wide);
        put_thunk(data + ordinal->mirror_offset, ordinal->ordinal_flags | 456, wide);
        memcpy(data + FUNCTION + 2, "ExitThreads", 11);
        executable_parse(data, sizeof(data), &info);
        ASSERT_EQ(info.status, EXE_OK);
        ASSERT_STR_EQ(find_row(&info, EXE_IMPORT, 0)->label, "KERNEL32.dll!ExitThreads");
        ASSERT_STR_EQ(find_row(&info, EXE_IMPORT, 1)->label, "KERNEL32.dll!#456");
        ASSERT(find_row(&info, EXE_IMPORT, 1)->editable);
        executable_free(&info);
    }
}

static void test_pe_unbound_iat_fallback(void) {
    for (int wide = 0; wide <= 1; ++wide) {
        uint8_t data[2048];
        make_pe(data, wide);
        put32(data + RAW, 0);
        executableInfo info = {0};
        executable_parse(data, sizeof(data), &info);
        ASSERT_EQ(info.status, EXE_OK);
        executableRow *named = find_row(&info, EXE_IMPORT, 0);
        executableRow *ordinal = find_row(&info, EXE_IMPORT, 1);
        ASSERT(named && ordinal);
        ASSERT_EQ(named->offset, (size_t)IAT);
        ASSERT_EQ(ordinal->ordinal_offset, ordinal->mirror_offset);
        ASSERT(named->editable && ordinal->editable);
        executable_free(&info);
    }
}

static void test_pe_bound_imports_are_read_only(void) {
    uint8_t data[2048];
    make_pe(data, 0);
    put32(data + RAW + 4, 1);
    put32(data + IAT, 0x76543210);
    put32(data + IAT + 4, 0x76543220);
    executableInfo info = {0};
    executable_parse(data, sizeof(data), &info);
    ASSERT_EQ(info.status, EXE_OK);
    ASSERT(!find_row(&info, EXE_MODULE, 0)->editable);
    ASSERT(!find_row(&info, EXE_IMPORT, 0)->editable);
    ASSERT(!find_row(&info, EXE_IMPORT, 1)->editable);
    put32(data + RAW, 0);
    executable_parse(data, sizeof(data), &info);
    ASSERT_EQ(info.status, EXE_OK);
    ASSERT(!find_row(&info, EXE_MODULE, 0)->editable);
    ASSERT(!find_row(&info, EXE_IMPORT, 0));
    ASSERT(strstr(info.message, "Bound"));
    executable_free(&info);
}

static void test_pe_bound_directory_disables_edits(void) {
    uint8_t data[2048];
    make_pe(data, 1);
    size_t bound = directories_offset(1) + 11 * 8;
    put32(data + bound, 0x1180);
    put32(data + bound + 4, 16);
    executableInfo info = {0};
    executable_parse(data, sizeof(data), &info);
    ASSERT_EQ(info.status, EXE_OK);
    ASSERT(!find_row(&info, EXE_MODULE, 0)->editable);
    ASSERT(!find_row(&info, EXE_IMPORT, 0)->editable);
    ASSERT(!find_row(&info, EXE_IMPORT, 1)->editable);
    executable_free(&info);
}

static void test_pe_mismatched_iat_is_read_only(void) {
    uint8_t data[2048];
    make_pe(data, 0);
    put32(data + IAT + 4, 0x80000055);
    executableInfo info = {0};
    executable_parse(data, sizeof(data), &info);
    ASSERT_EQ(info.status, EXE_OK);
    ASSERT(!find_row(&info, EXE_IMPORT, 1)->editable);
    executable_free(&info);
}

static void test_pe_header_rvas(void) {
    uint8_t data[2048];
    make_pe(data, 0);
    memcpy(data + 0x1a0, data + RAW, 40);
    put32(data + directories_offset(0) + 8, 0x1a0);
    put32(data + 0x1a0 + 12, 0x1d0);
    memcpy(data + 0x1d0, data + MODULE, 13);
    memcpy(data + 0x1e0, data + FUNCTION, 14);
    put32(data + LOOKUP, 0x1e0);
    put32(data + IAT, 0x1e0);
    executableInfo info = {0};
    executable_parse(data, sizeof(data), &info);
    ASSERT_EQ(info.status, EXE_OK);
    ASSERT_EQ(find_row(&info, EXE_MODULE, 0)->name_offset, (size_t)0x1d0);
    ASSERT_EQ(find_row(&info, EXE_IMPORT, 0)->name_offset, (size_t)0x1e2);
    ASSERT(find_row(&info, EXE_MODULE, 0)->editable);
    ASSERT(find_row(&info, EXE_IMPORT, 0)->editable);
    executable_free(&info);
}

static void test_pe_missing_import_directory(void) {
    uint8_t data[2048];
    make_pe(data, 0);
    put32(data + directories_offset(0) + 8, 0);
    put32(data + directories_offset(0) + 12, 0);
    executableInfo info = {0};
    executable_parse(data, sizeof(data), &info);
    ASSERT_EQ(info.status, EXE_OK);
    ASSERT(!find_row(&info, EXE_MODULE, 0));
    ASSERT(!find_row(&info, EXE_IMPORT, 0));
    executable_free(&info);
}

static void test_pe_rejects_invalid_directory_spans(void) {
    uint8_t data[2048];
    executableInfo info = {0};
    const uint32_t rvas[] = {0, 0x1400, UINT32_MAX};
    for (size_t i = 0; i < sizeof(rvas) / sizeof(rvas[0]); ++i) {
        make_pe(data, 0);
        put32(data + directories_offset(0) + 8, rvas[i]);
        executable_parse(data, sizeof(data), &info);
        ASSERT_EQ(info.status, EXE_MALFORMED);
    }
    make_pe(data, 0);
    put32(data + directories_offset(0) + 12, 20);
    executable_parse(data, sizeof(data), &info);
    ASSERT_EQ(info.status, EXE_MALFORMED);
    make_pe(data, 0);
    put32(data + directories_offset(0) + 12, UINT32_MAX);
    executable_parse(data, sizeof(data), &info);
    ASSERT_EQ(info.status, EXE_MALFORMED);
    executable_free(&info);
}

static void test_pe_rejects_invalid_thunks(void) {
    for (int wide = 0; wide <= 1; ++wide) {
        uint8_t data[2048];
        executableInfo info = {0};
        make_pe(data, wide);
        size_t width = wide ? 8 : 4;
        uint64_t flag = UINT64_C(1) << (width * 8 - 1);
        put_thunk(data + LOOKUP + width, flag | 0x10001, wide);
        executable_parse(data, sizeof(data), &info);
        ASSERT_EQ(info.status, EXE_MALFORMED);
        make_pe(data, wide);
        put_thunk(data + LOOKUP, 0x7fffffff, wide);
        executable_parse(data, sizeof(data), &info);
        ASSERT_EQ(info.status, EXE_MALFORMED);
        make_pe(data, wide);
        put32(data + RAW, (uint32_t)(0x1400 - width));
        put_thunk(data + 0x600 - width, flag | 123, wide);
        executable_parse(data, sizeof(data), &info);
        ASSERT_EQ(info.status, EXE_MALFORMED);
        make_pe(data, wide);
        put32(data + RAW + 16, (uint32_t)(0x1400 - width));
        put_thunk(data + 0x600 - width, 0x1120, wide);
        executable_parse(data, sizeof(data), &info);
        ASSERT_EQ(info.status, EXE_MALFORMED);
        make_pe(data, wide);
        put_thunk(data + IAT + width * 2, flag | 321, wide);
        executable_parse(data, sizeof(data), &info);
        ASSERT_EQ(info.status, EXE_MALFORMED);
        executable_free(&info);
    }
}

static void test_pe_rejects_truncated_strings_and_sections(void) {
    uint8_t data[2048];
    make_pe(data, 0);
    put32(data + RAW + 12, 0x13fc);
    memcpy(data + 0x5fc, "NAME", 4);
    executableInfo info = {0};
    executable_parse(data, sizeof(data), &info);
    ASSERT_EQ(info.status, EXE_MALFORMED);
    make_pe(data, 0);
    put32(data + LOOKUP, 0x13fc);
    memcpy(data + 0x5fe, "FN", 2);
    executable_parse(data, sizeof(data), &info);
    ASSERT_EQ(info.status, EXE_MALFORMED);
    make_pe(data, 0);
    put32(data + sections_offset(0) + 20, UINT32_MAX);
    executable_parse(data, sizeof(data), &info);
    ASSERT_EQ(info.status, EXE_MALFORMED);
    make_pe(data, 0);
    put32(data + directories_offset(0) - 4, UINT32_MAX);
    executable_parse(data, sizeof(data), &info);
    ASSERT_EQ(info.status, EXE_MALFORMED);
    executable_free(&info);
}

static void add_overlapping_section(uint8_t data[2048], uint32_t rva, uint32_t length) {
    size_t section = sections_offset(0) + 40;
    put16(data + HEADER + 6, 2);
    memcpy(data + section, ".alias", 6);
    put32(data + section + 8, length);
    put32(data + section + 12, rva);
    put32(data + section + 16, length);
    put32(data + section + 20, 0x600);
}

static void test_pe_rejects_ambiguous_rvas(void) {
    const uint32_t overlaps[] = {0x1000, 0x100a, 0x1100, 0x1105, 0x1125, 0x1044, 0x1084};
    for (size_t i = 0; i < sizeof(overlaps) / sizeof(overlaps[0]); ++i) {
        uint8_t data[2048];
        make_pe(data, 0);
        add_overlapping_section(data, overlaps[i], 16);
        executableInfo info = {0};
        executable_parse(data, sizeof(data), &info);
        ASSERT_EQ(info.status, EXE_MALFORMED);
        executable_free(&info);
    }
}

static void test_pe_delay_import_directory_is_explicit(void) {
    uint8_t data[2048];
    make_pe(data, 0);
    size_t directory = directories_offset(0) + 13 * 8;
    put32(data + directory, 0x13f0);
    put32(data + directory + 4, 16);
    executableInfo info = {0};
    executable_parse(data, sizeof(data), &info);
    ASSERT_EQ(info.status, EXE_OK);
    ASSERT(strstr(info.message, "delay"));
    executableRow *delay = find_row(&info, EXE_HEADER, 1);
    ASSERT(delay);
    ASSERT_EQ(delay->offset, (size_t)0x5f0);
    ASSERT(!delay->editable);
    put32(data + directory, 0x1400);
    executable_parse(data, sizeof(data), &info);
    ASSERT_EQ(info.status, EXE_MALFORMED);
    executable_free(&info);
}

static void test_pe_metadata_aliases_are_read_only(void) {
    /* Readable names may still occupy structural bytes. */
    const uint32_t aliases[] = {0, HEADER, 0x178, 0x100c, 0x1041, 0x1081};
    for (size_t i = 0; i < sizeof(aliases) / sizeof(aliases[0]); ++i) {
        uint8_t data[2048];
        make_pe(data, 0);
        put32(data + RAW + 12, aliases[i]);
        executableInfo info = {0};
        executable_parse(data, sizeof(data), &info);
        ASSERT_EQ(info.status, EXE_OK);
        executableRow *module = find_row(&info, EXE_MODULE, 0);
        ASSERT(module);
        ASSERT(!module->editable);
        ASSERT(strstr(module->label, "metadata overlap"));
        executable_free(&info);
    }
    uint8_t data[2048];
    make_pe(data, 0);
    /* Function's character bytes alias the address-table field. */
    put32(data + LOOKUP, 0x107e);
    put32(data + IAT, 0x107e);
    executableInfo info = {0};
    executable_parse(data, sizeof(data), &info);
    ASSERT_EQ(info.status, EXE_OK);
    ASSERT(!find_row(&info, EXE_IMPORT, 0)->editable);
    make_pe(data, 0);
    /* Ordinal encoding can itself inhabit the DOS header. */
    put32(data + RAW, 0x20);
    put32(data + 0x20, 0x8000007b);
    put32(data + IAT, 0x8000007b);
    put32(data + IAT + 4, 0);
    executable_parse(data, sizeof(data), &info);
    ASSERT_EQ(info.status, EXE_OK);
    ASSERT(!find_row(&info, EXE_IMPORT, 0)->editable);
    make_pe(data, 0);
    /* An otherwise normal ordinal's mirrored IAT cannot overwrite DOS. */
    put32(data + RAW + 16, 0x20);
    put32(data + LOOKUP, 0x8000007b);
    put32(data + LOOKUP + 4, 0);
    put32(data + 0x20, 0x8000007b);
    executable_parse(data, sizeof(data), &info);
    ASSERT_EQ(info.status, EXE_OK);
    ASSERT(!find_row(&info, EXE_IMPORT, 0)->editable);
    executable_free(&info);
}

static void test_pe_rejects_invalid_auxiliary_directories(void) {
    uint8_t data[2048];
    executableInfo info = {0};
    const unsigned indices[] = {11, 13};
    for (size_t i = 0; i < sizeof(indices) / sizeof(indices[0]); ++i) {
        make_pe(data, 0);
        size_t directory = directories_offset(0) + indices[i] * 8;
        put32(data + directory, 0x1180);
        executable_parse(data, sizeof(data), &info);
        ASSERT_EQ(info.status, EXE_MALFORMED);
        put32(data + directory + 4, 16);
        put32(data + directory, 0x1400);
        executable_parse(data, sizeof(data), &info);
        ASSERT_EQ(info.status, EXE_MALFORMED);
    }
    executable_free(&info);
}

static void test_pe_shared_bound_fields_stay_read_only(void) {
    uint8_t data[2048];
    make_pe(data, 0);
    memcpy(data + RAW + 20, data + RAW, 20);
    put32(data + RAW + 24, 1);
    put32(data + directories_offset(0) + 12, 60);
    executableInfo info = {0};
    executable_parse(data, sizeof(data), &info);
    ASSERT_EQ(info.status, EXE_OK);
    ASSERT(!find_row(&info, EXE_MODULE, 0)->editable);
    ASSERT(!find_row(&info, EXE_IMPORT, 0)->editable);
    ASSERT(!find_row(&info, EXE_IMPORT, 1)->editable);
    ASSERT(!find_row(&info, EXE_MODULE, 1)->editable);
    executable_free(&info);
}

int main(void) {
    RUN_TEST(test_pe32_and_pe64_name_and_ordinal_imports);
    RUN_TEST(test_pe_reparse_changed_name_and_mirrored_ordinal);
    RUN_TEST(test_pe_unbound_iat_fallback);
    RUN_TEST(test_pe_bound_imports_are_read_only);
    RUN_TEST(test_pe_bound_directory_disables_edits);
    RUN_TEST(test_pe_mismatched_iat_is_read_only);
    RUN_TEST(test_pe_header_rvas);
    RUN_TEST(test_pe_missing_import_directory);
    RUN_TEST(test_pe_rejects_invalid_directory_spans);
    RUN_TEST(test_pe_rejects_invalid_thunks);
    RUN_TEST(test_pe_rejects_truncated_strings_and_sections);
    RUN_TEST(test_pe_rejects_ambiguous_rvas);
    RUN_TEST(test_pe_delay_import_directory_is_explicit);
    RUN_TEST(test_pe_metadata_aliases_are_read_only);
    RUN_TEST(test_pe_rejects_invalid_auxiliary_directories);
    RUN_TEST(test_pe_shared_bound_fields_stay_read_only);
    TEST_REPORT();
}
