/*
 * pe_priv.h — Internal PE helper functions
 *
 * Private utility functions for working with PE images.
 * Not intended for inclusion outside the loader.
 */

#ifndef MY_WINE_PE_PRIV_H
#define MY_WINE_PE_PRIV_H

#include "include/pe.h"

/*
 * get_image_sections — Locate the IMAGE_SECTION_HEADER array within a loaded PE image.
 *
 * Given the base address of the mapped PE image and its NT headers,
 * returns a pointer to the first section header, computed from the DOS
 * header offset and the OptionalHeader size.
 */
static inline IMAGE_SECTION_HEADER *get_image_sections(void *base, const IMAGE_NT_HEADERS64 *nt)
{
    const IMAGE_DOS_HEADER *dos = (const IMAGE_DOS_HEADER *)base;
    uint32_t pe_off = dos->e_lfanew;
    uint32_t sec_off = pe_off + sizeof(uint32_t) + sizeof(IMAGE_FILE_HEADER) +
                       nt->FileHeader.SizeOfOptionalHeader;
    return (IMAGE_SECTION_HEADER *)((char *)base + sec_off);
}

#endif /* MY_WINE_PE_PRIV_H */
