/*
 * pe_imports.c — Import descriptor chain parsing
 *
 * Walks the import directory to count and validate import descriptors.
 */

#include <string.h>
#include <stdint.h>

#include "include/pe.h"
#include "include/pe_parser.h"
#include "include/pe_priv.h"

int parse_imports(const void *base, size_t file_size,
                  const IMAGE_NT_HEADERS64 *nt_headers,
                  IMAGE_IMPORT_DESCRIPTOR **out_first_descriptor)
{
    const IMAGE_DATA_DIRECTORY *imp_dir =
        &nt_headers->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];

    if (imp_dir->VirtualAddress == 0 || imp_dir->Size == 0)
        return 0; /* No imports */

    /* Get section table for RVA-to-offset conversion */
    const IMAGE_DOS_HEADER *dos = safe_ptr_at(base, 0, sizeof(IMAGE_DOS_HEADER), file_size);
    if (!dos)
        return -1;

    int sec_table_off = compute_section_table_offset(dos, nt_headers);
    uint16_t num_sections = nt_headers->FileHeader.NumberOfSections;
    size_t sec_table_size = num_sections * sizeof(IMAGE_SECTION_HEADER);

    if (sec_table_off < 0 || (size_t)sec_table_off + sec_table_size > file_size)
        return -1;

    const IMAGE_SECTION_HEADER *sections = safe_ptr_at(base, (size_t)sec_table_off, sec_table_size, file_size);
    if (!sections)
        return -1;

    /* Convert import directory RVA to file offset */
    int imp_offset = rva_to_offset(nt_headers, sections, imp_dir->VirtualAddress, file_size);
    if (imp_offset < 0)
        return -1;

    /* Walk the import descriptor chain */
    int count = 0;
    size_t current = (size_t)imp_offset;

    while (1) {
        if (current + sizeof(IMAGE_IMPORT_DESCRIPTOR) > file_size)
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

        count++;
        current += sizeof(IMAGE_IMPORT_DESCRIPTOR);

        /* Check we haven't gone past the import directory size */
        if (current > (size_t)imp_offset + imp_dir->Size)
            return -1;
    }

    if (count == 0)
        return 0;

    *out_first_descriptor = (IMAGE_IMPORT_DESCRIPTOR *)((const uint8_t *)base + (size_t)imp_offset);
    return count;
}
