#include "lhiew/executable.h"
#include "test_harness.h"

typedef struct linearFixture {
    uint8_t bytes[0x2400];
    size_t size, header, records, modules, procedures, end;
    size_t ordinal, list_record, second_page;
} linearFixture;

static void put16(uint8_t *p, uint16_t n) {
    p[0] = (uint8_t)n;
    p[1] = (uint8_t)(n >> 8);
}

static void put32(uint8_t *p, uint32_t n) {
    put16(p, (uint16_t)n);
    put16(p + 2, (uint16_t)(n >> 16));
}

/* Two real enumerated pages, one object, two module names, and fixups mixing
   name/ordinal imports with internal selector and entry-table references. */
static void make_fixture(linearFixture *f, int lx, int mz, unsigned ordinal_width) {
    memset(f, 0, sizeof(*f));
    f->header = mz ? 0x80 : 0;
    f->size = f->header + 0x2200;
    if (mz) {
        memcpy(f->bytes, "MZ", 2);
        put16(f->bytes + 2, (uint16_t)(f->size % 512));
        put16(f->bytes + 4, (uint16_t)((f->size + 511) / 512));
        put16(f->bytes + 8, 4);
        put16(f->bytes + 0x18, 0x40);
        put32(f->bytes + 0x3c, (uint32_t)f->header);
    }
    uint8_t *h = f->bytes + f->header;
    memcpy(h, lx ? "LX" : "LE", 2);
    put16(h + 8, 2);
    put16(h + 10, 1);
    put32(h + 0x14, 2);
    put32(h + 0x18, 1);
    put32(h + 0x20, 1);
    put32(h + 0x24, 8192);
    put32(h + 0x28, 4096);
    put32(h + 0x2c, lx ? 0 : 4096);
    put32(h + 0x38, 0x30);
    put32(h + 0x40, 0xb0);
    put32(h + 0x44, 1);
    put32(h + 0x48, 0xc8);
    put32(h + 0x68, 0xe0);
    put32(h + 0x6c, 0xec);
    put32(h + 0x70, 0x160);
    put32(h + 0x74, 2);
    put32(h + 0x78, 0x16f);
    put32(h + 0x80, (uint32_t)(f->header + 0x200));
    put32(h + 0xb0, 8192);
    put32(h + 0xb4, 0x1000);
    put32(h + 0xb8, 0x2005);
    put32(h + 0xbc, 1);
    put32(h + 0xc0, 2);
    if (lx) {
        put16(h + 0xcc, 4096);
        put32(h + 0xd0, 4096);
        put16(h + 0xd4, 4096);
    } else {
        h[0xca] = 1;
        h[0xce] = 2;
    }
    f->records = f->header + 0xec;
    size_t cursor = f->records;
    const uint8_t name[] = {7, 2, 0, 0, 1, 1, 0};
    memcpy(f->bytes + cursor, name, sizeof(name));
    cursor += sizeof(name);
    const uint8_t internal_selector[] = {2, 0, 2, 0, 1};
    memcpy(f->bytes + cursor, internal_selector, sizeof(internal_selector));
    cursor += sizeof(internal_selector);
    f->second_page = cursor;
    put32(h + 0xe4, (uint32_t)(cursor - f->records));
    f->bytes[cursor++] = 7;
    f->bytes[cursor++] = ordinal_width == 1 ? 0x81 : ordinal_width == 4 ? 0x11 : 1;
    put16(f->bytes + cursor, 4);
    cursor += 2;
    f->bytes[cursor++] = 2;
    f->ordinal = cursor;
    f->bytes[cursor] = 7;
    cursor += ordinal_width;
    f->list_record = cursor;
    const uint8_t list[] = {
        0x27, 0x76, 2, 1, 0, 1, 0, 0, 0, /* module16, name offset32 */
        0x78, 0x56, 0x34, 0x12, 4, 0, 8, 0 /* additive32, source list */
    };
    memcpy(f->bytes + cursor, list, sizeof(list));
    cursor += sizeof(list);
    const uint8_t internal_entry[] = {7, 3, 8, 0, 2};
    memcpy(f->bytes + cursor, internal_entry, sizeof(internal_entry));
    cursor += sizeof(internal_entry);
    put32(h + 0xe8, (uint32_t)(cursor - f->records));
    f->modules = f->header + 0x160;
    memcpy(f->bytes + f->modules, "\10" "DOSCALLS" "\5" "OTHER", 15);
    f->procedures = f->header + 0x16f;
    memcpy(f->bytes + f->procedures, "\0\7" "DosOpen" "\10" "DosClose", 18);
    f->end = f->procedures + 18;
    put32(h + 0x30, (uint32_t)(f->end - (f->header + 0xe0)));
    f->bytes[f->header + 0x200] = 0xc3;
}

static executableRow *find_row(executableInfo *info, executableRowKind kind, size_t index) {
    for (size_t i = 0; i < info->count; ++i)
        if (info->rows[i].kind == kind && index-- == 0) return &info->rows[i];
    return NULL;
}

static void test_linear_names_ordinals_and_wrappers(void) {
    for (int lx = 0; lx < 2; ++lx) {
        for (int mz = 0; mz < 2; ++mz) {
            for (unsigned width = 1; width <= 4; width *= 2) {
                linearFixture f;
                make_fixture(&f, lx, mz, width);
                executableInfo info = {0};
                executable_parse(f.bytes, f.size, &info);
                ASSERT_EQ(info.status, EXE_OK);
                ASSERT_STR_EQ(info.format, lx ? "LX" : "LE");
                executableRow *header = find_row(&info, EXE_HEADER, 0);
                executableRow *object = find_row(&info, EXE_REGION, 0);
                executableRow *module = find_row(&info, EXE_MODULE, 0);
                executableRow *name = find_row(&info, EXE_IMPORT, 0);
                executableRow *ordinal = find_row(&info, EXE_IMPORT, 1);
                executableRow *list = find_row(&info, EXE_IMPORT, 2);
                ASSERT(header && object && module && name && ordinal && list);
                ASSERT_EQ(header->offset, f.header);
                ASSERT_EQ(object->offset, f.header + 0xb0);
                ASSERT(strstr(object->label, "VA 00001000") != NULL);
                ASSERT_EQ(module->name_offset, f.modules + 1);
                ASSERT_EQ(module->name_length, 8);
                ASSERT_EQ(module->editable, 1);
                ASSERT_EQ(name->offset, f.records);
                ASSERT_EQ(name->name_offset, f.procedures + 2);
                ASSERT_EQ(name->name_length, 7);
                ASSERT_STR_EQ(name->label, "DOSCALLS!DosOpen");
                ASSERT_EQ(name->ordinal_offset, SIZE_MAX);
                ASSERT_STR_EQ(ordinal->label, "OTHER!#7");
                ASSERT_EQ(ordinal->ordinal_offset, f.ordinal);
                ASSERT_EQ(ordinal->ordinal_width, width);
                ASSERT_EQ(ordinal->ordinal, 7);
                ASSERT_EQ(ordinal->ordinal_max,
                          width == 1 ? UINT8_MAX : width == 2 ? UINT16_MAX : UINT32_MAX);
                ASSERT_EQ(list->offset, f.list_record);
                ASSERT_EQ(list->name_offset, name->name_offset);
                ASSERT_EQ(list->length, 17);
                ASSERT(find_row(&info, EXE_IMPORT, 3) == NULL);
                executable_free(&info);
            }
        }
    }
}

static void test_linear_in_place_name_and_ordinal_roundtrip(void) {
    linearFixture f;
    make_fixture(&f, 1, 1, 4);
    executableInfo info = {0};
    executable_parse(f.bytes, f.size, &info);
    ASSERT_EQ(info.status, EXE_OK);
    executableRow *module = find_row(&info, EXE_MODULE, 0);
    executableRow *name = find_row(&info, EXE_IMPORT, 0);
    executableRow *ordinal = find_row(&info, EXE_IMPORT, 1);
    ASSERT(module && name && ordinal);
    memcpy(f.bytes + module->name_offset, "NEWMODUL", module->name_length);
    memcpy(f.bytes + name->name_offset, "DosRead", name->name_length);
    put32(f.bytes + ordinal->ordinal_offset, 0x12345678);
    executable_parse(f.bytes, f.size, &info);
    ASSERT_EQ(info.status, EXE_OK);
    ASSERT_STR_EQ(find_row(&info, EXE_IMPORT, 0)->label, "NEWMODUL!DosRead");
    ASSERT_STR_EQ(find_row(&info, EXE_IMPORT, 2)->label, "NEWMODUL!DosRead");
    ASSERT_EQ(find_row(&info, EXE_IMPORT, 1)->ordinal, 0x12345678);
    executable_free(&info);
}

static void test_linear_empty_imports(void) {
    linearFixture f;
    make_fixture(&f, 0, 0, 1);
    put32(f.bytes + 0x30, 0);
    memset(f.bytes + 0x68, 0, 20);
    executableInfo info = {0};
    executable_parse(f.bytes, f.size, &info);
    ASSERT_EQ(info.status, EXE_OK);
    ASSERT(find_row(&info, EXE_REGION, 0) != NULL);
    ASSERT(find_row(&info, EXE_IMPORT, 0) == NULL);
    executable_free(&info);
}

static void test_linear_internal_fixups_without_import_tables(void) {
    linearFixture f;
    make_fixture(&f, 1, 0, 1);
    uint8_t record[] = {2, 0, 0, 0, 1};
    memcpy(f.bytes + f.records, record, sizeof(record));
    put32(f.bytes + 0xe4, sizeof(record));
    put32(f.bytes + 0xe8, sizeof(record));
    put32(f.bytes + 0x30, 12 + sizeof(record));
    memset(f.bytes + 0x70, 0, 12);
    executableInfo info = {0};
    executable_parse(f.bytes, f.size, &info);
    ASSERT_EQ(info.status, EXE_OK);
    ASSERT(find_row(&info, EXE_IMPORT, 0) == NULL);
    executable_free(&info);
}

static void test_linear_checksum_is_readonly(void) {
    linearFixture f;
    make_fixture(&f, 1, 0, 1);
    put32(f.bytes + 0x34, 0x1234);
    executableInfo info = {0};
    executable_parse(f.bytes, f.size, &info);
    ASSERT_EQ(info.status, EXE_OK);
    ASSERT(strstr(info.message, "checksum") != NULL);
    for (size_t i = 0; i < info.count; ++i) ASSERT_EQ(info.rows[i].editable, 0);
    executable_free(&info);
}

static void test_linear_truncated_headers_and_tables(void) {
    for (int lx = 0; lx < 2; ++lx) {
        linearFixture f;
        make_fixture(&f, lx, 1, 1);
        for (size_t length = f.header + 2; length < f.end; ++length) {
            executableInfo info = {0};
            executable_parse(f.bytes, length, &info);
            ASSERT_EQ(info.status, EXE_MALFORMED);
            executable_free(&info);
        }
    }
}

static void test_linear_invalid_fixups(void) {
    for (int variant = 0; variant < 9; ++variant) {
        linearFixture f;
        make_fixture(&f, 1, 0, 1);
        switch (variant) {
            case 0: f.bytes[f.records + 4] = 0; break;
            case 1: f.bytes[f.records + 4] = 3; break;
            case 2: f.bytes[f.records + 5] = 2; break; /* Inside a string. */
            case 3: put32(f.bytes + 0xe4, 6); break; /* Split record. */
            case 4: put32(f.bytes + 0xe8, 5); break; /* Descending page offsets. */
            case 5: f.bytes[f.list_record + 2] = 100; break; /* Truncated source list. */
            case 6: f.bytes[f.records + 1] |= 8; break; /* External chain flag. */
            case 7: put32(f.bytes + 0xe8, 0x1000); break;
            case 8: f.bytes[f.records + 5] = 0; break; /* Null procedure name. */
        }
        executableInfo info = {0};
        executable_parse(f.bytes, f.size, &info);
        ASSERT_EQ(info.status, EXE_MALFORMED);
        executable_free(&info);
    }
}

static void test_linear_invalid_tables(void) {
    for (int variant = 0; variant < 12; ++variant) {
        linearFixture f;
        make_fixture(&f, 0, 0, 1);
        switch (variant) {
            case 0: put32(f.bytes + 0x30, UINT32_MAX); break;
            case 1: put32(f.bytes + 0x70, 0x150); break;
            case 2: put32(f.bytes + 0x78, 0x100); break;
            case 3: f.bytes[f.modules] = 0x40; break;
            case 4: put32(f.bytes + 0x74, 1); break;
            case 5: put32(f.bytes + 0x40, UINT32_MAX); break;
            case 6: put32(f.bytes + 0xbc, 2); break;
            case 7: put32(f.bytes + 0x48, UINT32_MAX); break;
            case 8: f.bytes[f.procedures] = 1; break;
            case 9: put32(f.bytes + 0x40, 0x171); break; /* Object aliases DosOpen. */
            case 10: put32(f.bytes + 0x48, 0x161); break; /* Pages alias module name. */
            case 11: put32(f.bytes + 0x48, 0xb0); break; /* Page and object tables alias. */
        }
        executableInfo info = {0};
        executable_parse(f.bytes, f.size, &info);
        ASSERT_EQ(info.status, EXE_MALFORMED);
        for (size_t i = 0; i < info.count; ++i) ASSERT_EQ(info.rows[i].editable, 0);
        executable_free(&info);
    }
}

static void test_linear_unknown_encodings_and_work_limit(void) {
    for (int variant = 0; variant < 5; ++variant) {
        linearFixture f;
        make_fixture(&f, 1, 0, 1);
        switch (variant) {
            case 0: f.bytes[2] = 1; break;
            case 1: put32(f.bytes + 4, 1); break;
            case 2: f.bytes[f.records] = 1; break;
            case 3: f.bytes[f.procedures + 1] = 0x87; break;
            case 4: put32(f.bytes + 0x14, 1000001); break;
        }
        executableInfo info = {0};
        executable_parse(f.bytes, f.size, &info);
        ASSERT_EQ(info.status, variant == 4 ? EXE_LIMIT : EXE_UNSUPPORTED);
        executable_free(&info);
    }
}

int main(void) {
    RUN_TEST(test_linear_names_ordinals_and_wrappers);
    RUN_TEST(test_linear_in_place_name_and_ordinal_roundtrip);
    RUN_TEST(test_linear_empty_imports);
    RUN_TEST(test_linear_internal_fixups_without_import_tables);
    RUN_TEST(test_linear_checksum_is_readonly);
    RUN_TEST(test_linear_truncated_headers_and_tables);
    RUN_TEST(test_linear_invalid_fixups);
    RUN_TEST(test_linear_invalid_tables);
    RUN_TEST(test_linear_unknown_encodings_and_work_limit);
    TEST_REPORT();
}
