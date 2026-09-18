#include "lhiew/types.h"
#include "lhiew/binary.h"
#include "lhiew/executable.h"
#include "test_harness.h"

static void put32(uint8_t *p, uint32_t value) {
    for (size_t i = 0; i < 4; ++i) p[i] = (uint8_t)(value >> (8 * i));
}

/* Serialized independently from the parser, including auxiliary tables. */
static void make_nlm(uint8_t data[512]) {
    memset(data, 0, 512);
    memcpy(data, "NetWare Loadable Module\x1a", 24);
    put32(data + 24, 4);
    data[28] = 8;
    memcpy(data + 29, "TEST.NLM", 8);
    put32(data + 42, 256);
    put32(data + 46, 32);
    put32(data + 50, 288);
    put32(data + 54, 32);
    put32(data + 58, 16);
    put32(data + 62, 432);
    put32(data + 66, 4);
    put32(data + 70, 320);
    put32(data + 74, 1);
    put32(data + 78, 416);
    put32(data + 82, 1);
    put32(data + 86, 336);
    put32(data + 90, 2);
    put32(data + 94, 384);
    put32(data + 98, 1);
    put32(data + 102, 400);
    put32(data + 106, 1);
    put32(data + 110, 4);
    memset(data + 256, 0x90, 32);
    data[287] = 0xc3;
    data[320] = 8;
    memcpy(data + 321, "CLIB.NLM", 8);
    data[336] = 6;
    memcpy(data + 337, "printf", 6);
    put32(data + 343, 2);
    put32(data + 347, UINT32_C(0x40000004)); /* Code-relative fixup. */
    put32(data + 351, UINT32_C(0x80000008)); /* Data-absolute fixup. */
    data[355] = 4;
    memcpy(data + 356, "puts", 4);
    put32(data + 360, 0); /* Unreferenced global import is valid. */
    data[384] = 6;
    memcpy(data + 385, "start_", 6);
    put32(data + 391, UINT32_C(0x80000000));
    data[400] = 1;
    put32(data + 401, 0);
    data[405] = 5;
    memcpy(data + 406, "start", 5);
    put32(data + 416, UINT32_C(0xc0000008));
}

static void test_nlm_navigation_and_imports(void) {
    uint8_t data[512];
    make_nlm(data);
    executableInfo info = {0};
    executable_parse(data, sizeof(data), &info);
    ASSERT_EQ(info.status, EXE_OK);
    ASSERT_STR_EQ(info.format, "NLM");
    ASSERT_EQ(info.count, (size_t)6);
    ASSERT_EQ(info.rows[0].kind, EXE_HEADER);
    ASSERT_EQ(info.rows[0].offset, (size_t)0);
    ASSERT_EQ(info.rows[0].length, (size_t)150);
    ASSERT(!info.rows[0].editable);
    ASSERT_EQ(info.rows[1].kind, EXE_REGION);
    ASSERT_EQ(info.rows[1].offset, (size_t)256);
    ASSERT_EQ(info.rows[1].length, (size_t)32);
    ASSERT(strstr(info.rows[1].label, "0x104"));
    ASSERT_EQ(info.rows[2].offset, (size_t)288);
    ASSERT_EQ(info.rows[3].kind, EXE_MODULE);
    ASSERT_EQ(info.rows[3].offset, (size_t)320);
    ASSERT_EQ(info.rows[3].name_offset, (size_t)321);
    ASSERT_EQ(info.rows[3].name_length, (size_t)8);
    ASSERT(info.rows[3].editable);
    ASSERT(strstr(info.rows[3].label, "CLIB.NLM"));
    ASSERT_EQ(info.rows[4].kind, EXE_IMPORT);
    ASSERT_EQ(info.rows[4].offset, (size_t)336);
    ASSERT_EQ(info.rows[4].length, (size_t)19);
    ASSERT_EQ(info.rows[4].name_offset, (size_t)337);
    ASSERT_EQ(info.rows[4].name_length, (size_t)6);
    ASSERT_EQ(info.rows[4].ordinal_offset, SIZE_MAX);
    ASSERT(info.rows[4].editable);
    ASSERT(strstr(info.rows[4].label, "printf"));
    ASSERT(strstr(info.rows[4].label, "2 references"));
    ASSERT_EQ(info.rows[5].offset, (size_t)355);
    ASSERT_EQ(info.rows[5].name_offset, (size_t)356);
    ASSERT_EQ(info.rows[5].name_length, (size_t)4);
    executable_free(&info);
}

static void test_nlm_equal_length_renaming_preserves_references(void) {
    uint8_t data[512], original[512];
    make_nlm(data);
    memcpy(original, data, sizeof(data));
    executableInfo info = {0};
    executable_parse(data, sizeof(data), &info);
    ASSERT_EQ(info.status, EXE_OK);
    memcpy(data + info.rows[4].name_offset, "rename", info.rows[4].name_length);
    memcpy(data + info.rows[3].name_offset, "TEST.NLM", info.rows[3].name_length);
    executable_parse(data, sizeof(data), &info);
    ASSERT_EQ(info.status, EXE_OK);
    ASSERT(strstr(info.rows[4].label, "rename"));
    ASSERT(strstr(info.rows[3].label, "TEST.NLM"));
    ASSERT_EQ(memcmp(data + 343, original + 343, 169), 0);
    ASSERT_EQ(data[336], original[336]);
    executable_free(&info);
}

static void test_nlm_empty_tables_and_images(void) {
    uint8_t data[512];
    make_nlm(data);
    memset(data + 50, 0, 60);
    executableInfo info = {0};
    executable_parse(data, sizeof(data), &info);
    ASSERT_EQ(info.status, EXE_OK);
    ASSERT_EQ(info.count, (size_t)2);
    executable_free(&info);
}

static void test_nlm_unsupported_versions(void) {
    uint8_t data[512];
    make_nlm(data);
    executableInfo info = {0};
    put32(data + 24, 5);
    executable_parse(data, sizeof(data), &info);
    ASSERT_EQ(info.status, EXE_UNSUPPORTED);
    ASSERT_EQ(info.count, (size_t)0);
    executable_free(&info);
}

static void test_nlm_malformed_header_and_ranges(void) {
    const struct { size_t offset; uint32_t value; } cases[] = {
        {42, 500}, {46, UINT32_MAX}, {50, 500}, {54, UINT32_MAX},
        {62, 510}, {66, UINT32_MAX}, {70, UINT32_MAX}, {74, UINT32_MAX},
        {78, UINT32_MAX}, {82, UINT32_MAX}, {86, UINT32_MAX}, {90, UINT32_MAX},
        {94, UINT32_MAX}, {98, UINT32_MAX}, {102, UINT32_MAX}, {106, UINT32_MAX},
        {110, 32}, {114, 32}, {118, 32},
        {70, 28}, {86, 28}, {42, 100}, {50, 256}, /* Overlaps. */
        {343, UINT32_MAX}, /* Import-reference count exceeds file. */
        {347, UINT32_C(0x4000001d)}, /* Four-byte fixup crosses code end. */
        {351, UINT32_C(0x80000020)}, /* Four-byte fixup outside data. */
        {416, UINT32_C(0xc000001d)}, /* Internal fixup crosses code end. */
    };
    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i) {
        uint8_t data[512];
        make_nlm(data);
        put32(data + cases[i].offset, cases[i].value);
        executableInfo info = {0};
        executable_parse(data, sizeof(data), &info);
        ASSERT_EQ(info.status, EXE_MALFORMED);
        executable_free(&info);
    }
}

static void test_nlm_malformed_names_and_truncation(void) {
    const struct { size_t offset; uint8_t value; } cases[] = {
        {28, 0}, {28, 14}, {29, 0}, {37, 'X'},
        {130, 128}, {131, 'X'}, {146, 72}, {148, 18},
        {320, 0}, {320, 255}, {321, 0},
        {336, 0}, {336, 255}, {337, 0},
        {384, 255}, {400, 2}, {405, 255},
    };
    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i) {
        uint8_t data[512];
        make_nlm(data);
        data[cases[i].offset] = cases[i].value;
        executableInfo info = {0};
        executable_parse(data, sizeof(data), &info);
        ASSERT_EQ(info.status, EXE_MALFORMED);
        executable_free(&info);
    }
    uint8_t data[512];
    make_nlm(data);
    executableInfo info = {0};
    executable_parse(data, 129, &info);
    ASSERT_EQ(info.status, EXE_MALFORMED);
    executable_free(&info);
    put32(data + 86, 508);
    put32(data + 90, 1);
    data[508] = 1;
    data[509] = 'f';
    executable_parse(data, sizeof(data), &info);
    ASSERT_EQ(info.status, EXE_MALFORMED);
    executable_free(&info);
}

static void test_nlm_reference_inspection_limit(void) {
    size_t size = 100001 * 4 + 512;
    uint8_t *data = calloc(size, 1);
    ASSERT(data);
    make_nlm(data);
    put32(data + 343, 100001);
    executableInfo info = {0};
    executable_parse(data, size, &info);
    ASSERT_EQ(info.status, EXE_LIMIT);
    executable_free(&info);
    free(data);
}

static void test_nlm_maximum_header_strings_and_every_truncation(void) {
    uint8_t storage[513];
    uint8_t *data = storage + 1; /* Serialized fields need not be aligned. */
    make_nlm(data);
    memset(data + 42, 0, 88); /* No images or auxiliary tables. */
    size_t cursor = 130;
    const uint8_t lengths[] = {127, 71, 17};
    for (size_t i = 0; i < sizeof(lengths); ++i) {
        data[cursor++] = lengths[i];
        memset(data + cursor, 'A' + (int)i, lengths[i]);
        cursor += lengths[i];
        data[cursor++] = 0;
        if (i == 0) cursor += 14;
    }
    executableInfo info = {0};
    binaryInfo binary;
    executable_parse(data, cursor, &info);
    binary_detect(data, cursor, &binary);
    ASSERT_EQ(info.status, EXE_OK);
    ASSERT_EQ(binary.status, BINARY_DETECTED);
    ASSERT_EQ(info.count, (size_t)1);
    ASSERT_EQ(info.rows[0].length, cursor);
    ASSERT(!binary.has_entry);
    for (size_t size = 130; size < cursor; ++size) {
        executable_parse(data, size, &info);
        binary_detect(data, size, &binary);
        ASSERT_EQ(info.status, EXE_MALFORMED);
        ASSERT_EQ(binary.status, BINARY_MALFORMED);
    }
    data[cursor - 1] = 'X';
    executable_parse(data, cursor, &info);
    binary_detect(data, cursor, &binary);
    ASSERT_EQ(info.status, EXE_MALFORMED);
    ASSERT_EQ(binary.status, BINARY_MALFORMED);
    executable_free(&info);
}

static void test_nlm_percent_signs_are_literal_names(void) {
    uint8_t data[512];
    make_nlm(data);
    memcpy(data + 337, "f%s%n?", 6);
    executableInfo info = {0};
    executable_parse(data, sizeof(data), &info);
    ASSERT_EQ(info.status, EXE_OK);
    ASSERT_STR_EQ(info.rows[4].label, "Import: f%s%n? [global; 2 references]");
    ASSERT(info.rows[4].editable);
    executable_free(&info);
}

int main(void) {
    RUN_TEST(test_nlm_navigation_and_imports);
    RUN_TEST(test_nlm_equal_length_renaming_preserves_references);
    RUN_TEST(test_nlm_empty_tables_and_images);
    RUN_TEST(test_nlm_unsupported_versions);
    RUN_TEST(test_nlm_malformed_header_and_ranges);
    RUN_TEST(test_nlm_malformed_names_and_truncation);
    RUN_TEST(test_nlm_reference_inspection_limit);
    RUN_TEST(test_nlm_maximum_header_strings_and_every_truncation);
    RUN_TEST(test_nlm_percent_signs_are_literal_names);
    TEST_REPORT();
}
