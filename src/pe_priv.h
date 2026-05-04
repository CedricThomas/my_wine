#ifndef MY_WINE_PE_PRIV_H
#define MY_WINE_PE_PRIV_H

#include <string.h>
#include <stdint.h>
#include <stddef.h>

#include "include/pe.h"

/* Return a pointer at a file offset, or NULL if offset + len exceeds file_size */
static inline const void *safe_ptr_at(const void *base, size_t offset, size_t len, size_t file_size)
{
    if (len > file_size || offset > file_size - len)
        return NULL;
    return (const uint8_t *)base + offset;
}

/* Convert RVA to file offset using the section table. Returns -1 if not mappable. */
static inline int rva_to_offset(const IMAGE_NT_HEADERS64 *nt, const IMAGE_SECTION_HEADER *sections,
                         uint32_t rva, size_t file_size)
{
    uint16_t num = nt->FileHeader.NumberOfSections;
    for (uint16_t i = 0; i < num; i++) {
        const IMAGE_SECTION_HEADER *sec = &sections[i];
        uint32_t sec_start = sec->VirtualAddress;
        uint32_t sec_end   = sec_start + sec->Misc.VirtualSize;
        if (rva >= sec_start && rva < sec_end) {
            size_t off = (size_t)(sec->PointerToRawData + (rva - sec_start));
            if (off <= file_size)
                return (int)off;
            return -1;
        }
    }
    /* Might be in headers before first section */
    if (rva < nt->OptionalHeader.SizeOfHeaders)
        return (int)rva;
    return -1;
}

/* Derive section table offset from DOS header and NT headers */
static inline int compute_section_table_offset(const IMAGE_DOS_HEADER *dos,
                                        const IMAGE_NT_HEADERS64 *nt)
{
    uint32_t pe_off = dos->e_lfanew;
    size_t sec_off = (size_t)pe_off + sizeof(uint32_t) + sizeof(IMAGE_FILE_HEADER) +
                     (size_t)nt->FileHeader.SizeOfOptionalHeader;
    return (int)sec_off;
}

#endif /* MY_WINE_PE_PRIV_H */
