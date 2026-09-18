#include "lhiew/types.h"
#include "executable_internal.h"

#include <string.h>

/* https://learn.microsoft.com/en-us/windows/win32/debug/pe-format */
typedef struct peSection {
    uint32_t rva, length;
    size_t offset;
} peSection;

typedef struct peView {
    const uint8_t *data;
    size_t size, sections, section_count, headers;
    unsigned width;
    int bound;
    peSection *regions;
    size_t region_count;
} peView;

enum { PE_NAME = 1, PE_ORDINAL = 2 };

typedef struct peGuard {
    size_t start, end, name_end, ordinal_end;
    unsigned kinds;
} peGuard;

typedef struct peGuards {
    peGuard *spans;
    size_t count, capacity;
} peGuards;

/* Distinct RVA mappings can still alias physical bytes. Keep structural
 * fields browseable, but never let a semantic edit overwrite those fields. */
static int pe_guard(peGuards *guards, executableInfo *info, size_t start,
                    size_t length, unsigned kinds) {
    if (!length) return 1;
    if (guards->count == 500000)
        return exe_error(info, EXE_LIMIT, "PE metadata inspection limit reached");
    if (guards->count == guards->capacity) {
        size_t capacity = guards->capacity ? guards->capacity * 2 : 64;
        peGuard *spans = realloc(guards->spans, capacity * sizeof(*spans));
        if (!spans)
            return exe_error(info, EXE_LIMIT, "Not enough memory for PE metadata");
        guards->spans = spans;
        guards->capacity = capacity;
    }
    guards->spans[guards->count++] = (peGuard){start, start + length, 0, 0, kinds};
    return 1;
}

static int pe_compare_guards(const void *a, const void *b) {
    const peGuard *left = a, *right = b;
    return left->start < right->start ? -1 : left->start > right->start;
}

static int pe_guarded(const peGuards *guards, size_t start, size_t length, unsigned kind) {
    size_t left = 0, right = guards->count;
    while (left < right) {
        size_t middle = left + (right - left) / 2;
        if (guards->spans[middle].start < start + length) left = middle + 1;
        else right = middle;
    }
    if (!left) return 0;
    const peGuard *last = &guards->spans[left - 1];
    return (kind == PE_NAME ? last->name_end : last->ordinal_end) > start;
}

static void pe_read_only(executableRow *row, const char *reason) {
    row->editable = 0;
    size_t length = strlen(row->label);
    snprintf(row->label + length, sizeof(row->label) - length, " [%s/read-only]", reason);
}

static void pe_guard_edits(peGuards *guards, executableInfo *info) {
    qsort(guards->spans, guards->count, sizeof(*guards->spans), pe_compare_guards);
    size_t name_end = 0, ordinal_end = 0;
    for (size_t i = 0; i < guards->count; ++i) {
        peGuard *span = &guards->spans[i];
        if ((span->kinds & PE_NAME) && span->end > name_end) name_end = span->end;
        if ((span->kinds & PE_ORDINAL) && span->end > ordinal_end) ordinal_end = span->end;
        span->name_end = name_end;
        span->ordinal_end = ordinal_end;
    }
    for (size_t i = 0; i < info->count; ++i) {
        executableRow *row = &info->rows[i];
        if (!row->editable) continue;
        int collision = row->name_offset != SIZE_MAX &&
            pe_guarded(guards, row->name_offset, row->name_length, PE_NAME);
        if (row->ordinal_offset != SIZE_MAX) {
            collision |= pe_guarded(guards, row->ordinal_offset, row->ordinal_width, PE_ORDINAL);
            if (row->mirror_offset != SIZE_MAX)
                collision |= pe_guarded(guards, row->mirror_offset, row->ordinal_width, PE_ORDINAL);
        }
        if (collision) {
            pe_read_only(row, "metadata overlap");
            snprintf(info->message, sizeof(info->message), "Overlapping metadata fields are read-only");
        }
    }
}

/* A metadata item must occupy one unambiguous, file-backed RVA interval. */
static int pe_rva(const peView *pe, uint32_t rva, size_t length,
                  size_t *offset, size_t *end) {
    if (rva < pe->headers) {
        if (!byte_span(pe->headers, rva, length)) return 0;
        *offset = rva;
        *end = pe->headers;
        return 1;
    }
    size_t low = 0, high = pe->region_count;
    while (low < high) {
        size_t mid = low + (high - low) / 2;
        if (pe->regions[mid].rva <= rva) low = mid + 1;
        else high = mid;
    }
    if (!low) return 0;
    const peSection *region = &pe->regions[low - 1];
    size_t delta = rva - region->rva;
    if (delta >= region->length || length > region->length - delta) return 0;
    *offset = region->offset + delta;
    *end = region->offset + region->length;
    return 1;
}

static int pe_string(const peView *pe, uint32_t rva, unsigned skip,
                     char name[256], size_t *offset, size_t *length) {
    size_t start, end;
    if (!pe_rva(pe, rva, skip + 1, &start, &end)) return 0;
    return exe_name(pe->data, pe->size, start + skip, end, 0,
                    name, offset, length) && *length;
}

static int pe_import_rows(const peView *pe, uint32_t rva, uint32_t length,
                          executableInfo *info, peGuards *guards) {
    if (!rva && !length) return 1;
    size_t directory, end;
    if (!rva || length < 20 || !pe_rva(pe, rva, length, &directory, &end))
        return exe_error(info, EXE_MALFORMED, "Invalid PE import directory span");
    if (!pe_guard(guards, info, directory, length, PE_NAME | PE_ORDINAL)) return 0;
    end = directory + length;
    for (size_t descriptor = directory; byte_span(end, descriptor, 20); descriptor += 20) {
        const uint8_t *record = pe->data + descriptor;
        uint32_t lookup = read_le32(record), stamp = read_le32(record + 4);
        uint32_t name_rva = read_le32(record + 12), iat = read_le32(record + 16);
        if (!lookup && !stamp && !read_le32(record + 8) && !name_rva && !iat)
            return 1;
        char module[256];
        size_t name_offset, name_length;
        if (!pe_string(pe, name_rva, 0, module, &name_offset, &name_length) || !iat)
            return exe_error(info, EXE_MALFORMED, "Invalid PE imported module");
        int editable = !pe->bound && !stamp;
        executableRow *row = exe_add(info, EXE_MODULE, descriptor, 20,
                                     "Module %s%s", module, editable ? "" : " [bound/read-only]");
        if (!row) return 0;
        row->name_offset = name_offset;
        row->name_length = name_length;
        row->editable = editable;
        size_t module_row = info->count - 1;
        if (!pe_guard(guards, info, name_offset, name_length + 1,
                      PE_ORDINAL | (editable ? 0 : PE_NAME))) return 0;
        if (!lookup && !editable) {
            size_t bound_iat, bound_end;
            if (!pe_rva(pe, iat, pe->width, &bound_iat, &bound_end))
                return exe_error(info, EXE_MALFORMED, "Invalid bound PE import address table");
            int terminated = 0;
            for (size_t field = bound_iat; byte_span(bound_end, field, pe->width); field += pe->width) {
                if (!pe_guard(guards, info, field, pe->width, PE_NAME | PE_ORDINAL)) return 0;
                uint64_t value = pe->width == 8 ? read_le64(pe->data + field) : read_le32(pe->data + field);
                if (!value) { terminated = 1; break; }
            }
            if (!terminated)
                return exe_error(info, EXE_MALFORMED, "Unterminated bound PE import address table");
            snprintf(info->message, sizeof(info->message), "Bound IAT without lookup table: symbols unavailable");
            continue;
        }
        size_t table, table_end, iat_offset, iat_end;
        if (!pe_rva(pe, lookup ? lookup : iat, pe->width, &table, &table_end) ||
            !pe_rva(pe, iat, pe->width, &iat_offset, &iat_end))
            return exe_error(info, EXE_MALFORMED, "Invalid PE import lookup/address table");
        int terminated = 0;
        for (size_t index = 0; index < (table_end - table) / pe->width; ++index) {
            size_t field = table + index * pe->width;
            if (index >= (iat_end - iat_offset) / pe->width)
                return exe_error(info, EXE_MALFORMED, "Truncated PE import address table");
            size_t mirror = iat_offset + index * pe->width;
            if (!pe_guard(guards, info, field, pe->width, PE_NAME) ||
                !pe_guard(guards, info, mirror, pe->width, PE_NAME)) return 0;
            uint64_t value = pe->width == 8 ? read_le64(pe->data + field) : read_le32(pe->data + field);
            uint64_t actual = pe->width == 8 ? read_le64(pe->data + mirror) : read_le32(pe->data + mirror);
            if (!value) {
                if (!actual) terminated = 1;
                break;
            }
            /* An unbound IAT must agree with the lookup table before editing. */
            int field_editable = editable && actual == value;
            if (editable && actual != value) {
                executableRow *module_entry = &info->rows[module_row];
                if (module_entry->editable) {
                    pe_read_only(module_entry, "IAT mismatch");
                    if (!pe_guard(guards, info, module_entry->name_offset,
                                  module_entry->name_length + 1, PE_NAME)) return 0;
                }
                snprintf(info->message, sizeof(info->message), "Inconsistent import lookup/address fields are read-only");
            }
            uint64_t flag = UINT64_C(1) << (pe->width * 8 - 1);
            if (value & flag) {
                if (value & ~(flag | UINT64_C(0xffff)))
                    return exe_error(info, EXE_MALFORMED, "Reserved bits in PE import ordinal");
                row = exe_add(info, EXE_IMPORT, field, pe->width,
                              "%s!#%u", module, (unsigned)(value & 0xffff));
                if (!row) return 0;
                row->ordinal_offset = field;
                row->ordinal_width = pe->width;
                row->ordinal = value & 0xffff;
                row->ordinal_max = 0xffff;
                row->ordinal_flags = flag;
                row->mirror_offset = mirror;
            } else {
                char function[256];
                if (value > 0x7fffffff ||
                    !pe_string(pe, (uint32_t)value, 2, function, &name_offset, &name_length))
                    return exe_error(info, EXE_MALFORMED, "Invalid PE imported function name");
                row = exe_add(info, EXE_IMPORT, field, pe->width, "%s!%s", module, function);
                if (!row) return 0;
                row->name_offset = name_offset;
                row->name_length = name_length;
                if (!pe_guard(guards, info, name_offset - 2, 2, PE_NAME | PE_ORDINAL) ||
                    !pe_guard(guards, info, name_offset, name_length + 1,
                              PE_ORDINAL | (field_editable ? 0 : PE_NAME))) return 0;
            }
            row->editable = field_editable;
            if (!field_editable) {
                pe_read_only(row, editable ? "IAT mismatch" : "bound");
                if (!pe_guard(guards, info, field, pe->width, PE_ORDINAL) ||
                    !pe_guard(guards, info, mirror, pe->width, PE_ORDINAL)) return 0;
            }
        }
        if (!terminated)
            return exe_error(info, EXE_MALFORMED, "Unterminated PE import lookup/address table");
    }
    return exe_error(info, EXE_MALFORMED, "Unterminated PE import directory");
}

static int pe_imports(const peView *pe, uint32_t rva, uint32_t length,
                      executableInfo *info) {
    peGuards guards = {0};
    int ok = 1;
    if (info->count && info->rows[0].offset)
        ok = pe_guard(&guards, info, 0, 64, PE_NAME | PE_ORDINAL);
    for (size_t i = 0; ok && i < info->count; ++i) {
        const executableRow *row = &info->rows[i];
        if (row->kind == EXE_HEADER)
            ok = pe_guard(&guards, info, row->offset, row->length, PE_NAME | PE_ORDINAL);
    }
    if (ok) ok = pe_guard(&guards, info, pe->sections, pe->section_count * 40, PE_NAME | PE_ORDINAL);
    if (ok) ok = pe_import_rows(pe, rva, length, info, &guards);
    if (ok) pe_guard_edits(&guards, info);
    free(guards.spans);
    return ok;
}

static int pe_section_compare(const void *a, const void *b) {
    const peSection *left = a, *right = b;
    return left->rva < right->rva ? -1 : left->rva > right->rva;
}

/* Validate once, retaining disjoint RVA mappings for logarithmic lookup.
   Browser rows remain in file table order, independent of the sorted index. */
static int pe_sections(peView *pe, executableInfo *info) {
    if (!pe->section_count) return 1;
    pe->regions = malloc(pe->section_count * sizeof(*pe->regions));
    if (!pe->regions)
        return exe_error(info, EXE_LIMIT, "Not enough memory for PE section mappings");
    for (size_t i = 0; i < pe->section_count; ++i) {
        const uint8_t *section = pe->data + pe->sections + i * 40;
        size_t offset = read_le32(section + 20), length = read_le32(section + 16);
        if (!byte_span(pe->size, offset, length))
            return exe_error(info, EXE_MALFORMED, "PE section extends past file");
        uint32_t va = read_le32(section + 12), mapped = read_le32(section + 16);
        uint32_t virtual_size = read_le32(section + 8);
        if (virtual_size && virtual_size < mapped) mapped = virtual_size;
        if ((uint64_t)va + mapped > UINT64_C(0x100000000) ||
            (mapped && va < pe->headers))
            return exe_error(info, EXE_MALFORMED, "Ambiguous PE header/section RVA mapping");
        if (mapped) pe->regions[pe->region_count++] = (peSection){va, mapped, offset};
        char name[9];
        memcpy(name, section, 8); name[8] = '\0';
        if (!exe_add(info, EXE_REGION, length ? offset : pe->sections + i * 40,
                     length ? length : 40, "Section %s RVA=%08x size=%zx flags=%08x",
                     name, read_le32(section + 12), length, read_le32(section + 36))) return 0;
    }
    qsort(pe->regions, pe->region_count, sizeof(*pe->regions), pe_section_compare);
    for (size_t i = 1; i < pe->region_count; ++i) {
        const peSection *previous = &pe->regions[i - 1], *current = &pe->regions[i];
        if ((uint64_t)previous->rva + previous->length > current->rva)
            return exe_error(info, EXE_MALFORMED, "Overlapping PE section RVA mappings");
    }
    return 1;
}

static int pe_directories(peView *pe, size_t optional, size_t fixed,
                           uint32_t directories, executableInfo *info) {
    if (directories > 11) {
        const uint8_t *bound = pe->data + optional + fixed + 11 * 8;
        pe->bound = read_le32(bound) || read_le32(bound + 4);
        if (pe->bound) {
            size_t offset, end;
            if (!read_le32(bound) || read_le32(bound + 4) < 8 ||
                !pe_rva(pe, read_le32(bound), read_le32(bound + 4), &offset, &end))
                return exe_error(info, EXE_MALFORMED, "Invalid PE bound import directory");
            if (!exe_add(info, EXE_HEADER, offset, read_le32(bound + 4), "Bound import directory (raw)")) return 0;
        }
    }
    if (directories > 13) {
        const uint8_t *delay = pe->data + optional + fixed + 13 * 8;
        if (read_le32(delay) || read_le32(delay + 4)) {
            snprintf(info->message, sizeof(info->message), "Standard imports shown; delay imports are not decoded");
            size_t offset, end;
            if (read_le32(delay) && read_le32(delay + 4) &&
                pe_rva(pe, read_le32(delay), read_le32(delay + 4), &offset, &end)) {
                if (!exe_add(info, EXE_HEADER, offset, read_le32(delay + 4), "Delay import directory (raw)")) return 0;
            } else {
                return exe_error(info, EXE_MALFORMED, "Invalid PE delay import directory");
            }
        }
    }
    if (pe->bound)
        snprintf(info->message, sizeof(info->message), "Bound imports are browse-only");
    if (directories < 2) return 1;
    const uint8_t *imports = pe->data + optional + fixed + 8;
    return pe_imports(pe, read_le32(imports), read_le32(imports + 4), info);
}

int exe_parse_pe(const uint8_t *data, size_t size, size_t header, executableInfo *info) {
    snprintf(info->format, sizeof(info->format), "PE");
    if (!byte_span(size, header, 24))
        return exe_error(info, EXE_MALFORMED, "Truncated PE header");
    size_t optional = header + 24, optional_size = read_le16(data + header + 20);
    if (!byte_span(size, optional, optional_size) || optional_size < 2)
        return exe_error(info, EXE_MALFORMED, "Truncated PE optional header");
    uint16_t magic = read_le16(data + optional);
    if (magic != 0x10b && magic != 0x20b)
        return exe_error(info, EXE_UNSUPPORTED, "Unsupported PE optional header");
    int wide = magic == 0x20b;
    size_t fixed = wide ? 112 : 96;
    if (optional_size < fixed)
        return exe_error(info, EXE_MALFORMED, "Truncated PE data directories");
    uint32_t directories = read_le32(data + optional + fixed - 4);
    if (directories > (optional_size - fixed) / 8)
        return exe_error(info, EXE_MALFORMED, "PE directory count exceeds optional header");
    peView pe = {data, size, optional + optional_size,
                 read_le16(data + header + 6), read_le32(data + optional + 60),
                 wide ? 8u : 4u, 0, NULL, 0};
    if (!byte_span(size, pe.sections, pe.section_count * 40) || pe.headers > size)
        return exe_error(info, EXE_MALFORMED, "Invalid PE section/header span");
    if (pe.section_count > 4096)
        return exe_error(info, EXE_LIMIT, "PE section count exceeds browser limit (4096)");
    snprintf(info->format, sizeof(info->format), "%s", wide ? "PE32+" : "PE32");
    if (!exe_add(info, EXE_HEADER, header, 24 + optional_size,
                 "%s machine=%04x sections=%zu entry RVA=%08x",
                 info->format, read_le16(data + header + 4), pe.section_count,
                 read_le32(data + optional + 16))) return 0;
    int ok = pe_sections(&pe, info) &&
             pe_directories(&pe, optional, fixed, directories, info);
    free(pe.regions);
    return ok;
}
