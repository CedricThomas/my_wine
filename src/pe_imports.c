/*
 * pe_imports.c — Import descriptor chain parsing
 *
 * Walks the import directory to count and validate import descriptors.
 * Supports both PE32 (32-bit thunks) and PE32+ (64-bit thunks).
 */

#include <string.h>
#include <stdint.h>

#include "include/pe.h"
#include "include/pe_parser.h"
#include "src/pe_priv.h"

static int has_bounded_cstr(const void *base, size_t file_size, size_t offset)
{
    const char *s = safe_ptr_at(base, offset, 1, file_size);
    if (!s)
        return 0;
    for (size_t i = offset; i < file_size; i++) {
        if (((const char *)base)[i] == '\0')
            return 1;
    }
    return 0;
}

int parse_imports(const void *base, size_t file_size,
                  const IMAGE_NT_HEADERS *nt_headers,
                  IMAGE_IMPORT_DESCRIPTOR **out_first_descriptor)
{
    IMAGE_DATA_DIRECTORY imp_dir;
    if (!pe_get_import_dir(nt_headers, &imp_dir))
        return 0; /* No imports */

    if (imp_dir.VirtualAddress == 0 || imp_dir.Size == 0)
        return 0; /* No imports */

    /* Get section table for RVA-to-offset conversion */
    const IMAGE_DOS_HEADER *dos = safe_ptr_at(base, 0, sizeof(IMAGE_DOS_HEADER), file_size);
    if (!dos)
        return -1;

    int sec_table_off = compute_section_table_offset(dos, nt_headers);
    uint16_t num_sections = pe_section_count(nt_headers);
    size_t sec_table_size = num_sections * sizeof(IMAGE_SECTION_HEADER);

    if (sec_table_off < 0 || (size_t)sec_table_off + sec_table_size > file_size)
        return -1;

    const IMAGE_SECTION_HEADER *sections = safe_ptr_at(base, (size_t)sec_table_off, sec_table_size, file_size);
    if (!sections)
        return -1;

    /* Convert import directory RVA to file offset */
    int imp_offset = rva_range_to_offset(nt_headers, sections,
                                         imp_dir.VirtualAddress,
                                         imp_dir.Size,
                                         file_size);
    if (imp_offset < 0)
        return -1;

    /*
     * Walk the import descriptor chain.
     *
     * IMAGE_IMPORT_DESCRIPTOR is 20 bytes for both PE32 and PE32+
     * (all fields are 32-bit RVAs per the MS PE/COFF specification).
     * The difference is in the thunk data pointed to by the descriptors:
     *   PE32:  IMAGE_THUNK_DATA32 (4 bytes each)
     *   PE32+: IMAGE_THUNK_DATA64 (8 bytes each)
     */

    size_t thunk_size = pe_is_pe32(nt_headers)
                        ? sizeof(IMAGE_THUNK_DATA32)
                        : sizeof(IMAGE_THUNK_DATA64);

    int count = 0;
    size_t current = (size_t)imp_offset;
    size_t import_end = (size_t)imp_offset + imp_dir.Size;

    while (1) {
        if (current > import_end ||
            sizeof(IMAGE_IMPORT_DESCRIPTOR) > import_end - current)
            return -1;

        const IMAGE_IMPORT_DESCRIPTOR *desc = safe_ptr_at(base, current,
                                                          sizeof(IMAGE_IMPORT_DESCRIPTOR), file_size);
        if (!desc)
            return -1;

        /* Termination: Name field is zero */
        if (desc->Name == 0)
            break;

        /* Validate Name RVA is accessible */
        int name_off = rva_to_offset(nt_headers, sections, desc->Name, file_size);
        if (name_off < 0)
            return -1;
        if (!has_bounded_cstr(base, file_size, (size_t)name_off))
            return -1;

        /* Validate OriginalFirstThunk RVA (points to ILT) */
        if (desc->u1.OriginalFirstThunk != 0) {
            int ilt_off = rva_to_offset(nt_headers, sections, desc->u1.OriginalFirstThunk, file_size);
            if (ilt_off < 0)
                return -1;
            /* Ensure at least one thunk entry is readable */
            if ((size_t)ilt_off + thunk_size > file_size)
                return -1;
        }

        /* Validate FirstThunk RVA (points to IAT) */
        if (desc->FirstThunk != 0) {
            int iat_off = rva_to_offset(nt_headers, sections, desc->FirstThunk, file_size);
            if (iat_off < 0)
                return -1;
            if ((size_t)iat_off + thunk_size > file_size)
                return -1;
        }

        count++;
        current += sizeof(IMAGE_IMPORT_DESCRIPTOR);
    }

    if (count == 0)
        return 0;

    *out_first_descriptor = (IMAGE_IMPORT_DESCRIPTOR *)((const uint8_t *)base + (size_t)imp_offset);
    return count;
}
