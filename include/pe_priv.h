/*
 * pe_priv.h — Internal PE helper functions
 *
 * Private utility functions for working with PE images.
 * Not intended for inclusion outside the loader.
 */

#ifndef MY_WINE_PE_PRIV_H
#define MY_WINE_PE_PRIV_H

#include <stdbool.h>
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

/* ── PE32/PE64 type checks ─────────────────────────────────────── */

static inline bool pe_is_pe32(const IMAGE_NT_HEADERS *nt)
{
    return nt->pe_type == PE_TYPE_32;
}

static inline bool pe_is_pe64(const IMAGE_NT_HEADERS *nt)
{
    return nt->pe_type == PE_TYPE_64;
}

/* ── Accessor functions for OptionalHeader fields ───────────────── */

static inline uint64_t pe_image_base(const IMAGE_NT_HEADERS *nt)
{
    if (pe_is_pe32(nt))
        return (uint64_t)nt->u.nt32.OptionalHeader.ImageBase;
    else
        return (uint64_t)nt->u.nt64.OptionalHeader.ImageBase;
}

static inline uint32_t pe_entry_rva(const IMAGE_NT_HEADERS *nt)
{
    if (pe_is_pe32(nt))
        return nt->u.nt32.OptionalHeader.AddressOfEntryPoint;
    else
        return nt->u.nt64.OptionalHeader.AddressOfEntryPoint;
}

static inline uint64_t pe_stack_reserve(const IMAGE_NT_HEADERS *nt)
{
    if (pe_is_pe32(nt))
        return (uint64_t)nt->u.nt32.OptionalHeader.SizeOfStackReserve;
    else
        return nt->u.nt64.OptionalHeader.SizeOfStackReserve;
}

static inline uint16_t pe_section_count(const IMAGE_NT_HEADERS *nt)
{
    if (pe_is_pe32(nt))
        return nt->u.nt32.FileHeader.NumberOfSections;
    else
        return nt->u.nt64.FileHeader.NumberOfSections;
}

static inline uint32_t pe_size_of_image(const IMAGE_NT_HEADERS *nt)
{
    if (pe_is_pe32(nt))
        return nt->u.nt32.OptionalHeader.SizeOfImage;
    else
        return nt->u.nt64.OptionalHeader.SizeOfImage;
}

static inline uint32_t pe_size_of_headers(const IMAGE_NT_HEADERS *nt)
{
    if (pe_is_pe32(nt))
        return nt->u.nt32.OptionalHeader.SizeOfHeaders;
    else
        return nt->u.nt64.OptionalHeader.SizeOfHeaders;
}

static inline uint32_t pe_section_alignment(const IMAGE_NT_HEADERS *nt)
{
    if (pe_is_pe32(nt))
        return nt->u.nt32.OptionalHeader.SectionAlignment;
    else
        return nt->u.nt64.OptionalHeader.SectionAlignment;
}

static inline uint16_t pe_optional_header_size(const IMAGE_NT_HEADERS *nt)
{
    if (pe_is_pe32(nt))
        return nt->u.nt32.FileHeader.SizeOfOptionalHeader;
    else
        return nt->u.nt64.FileHeader.SizeOfOptionalHeader;
}

/* ── Data directory accessors ───────────────────────────────────── */

/* Get a data directory entry by index. Returns true on success. */
static inline bool pe_get_data_dir(const IMAGE_NT_HEADERS *nt,
                                    uint16_t index,
                                    IMAGE_DATA_DIRECTORY *out)
{
    if (pe_is_pe32(nt)) {
        if (index >= nt->u.nt32.OptionalHeader.NumberOfRvaAndSizes)
            return false;
        *out = nt->u.nt32.OptionalHeader.DataDirectory[index];
    } else {
        if (index >= nt->u.nt64.OptionalHeader.NumberOfRvaAndSizes)
            return false;
        *out = nt->u.nt64.OptionalHeader.DataDirectory[index];
    }
    return true;
}

/* Convenience: get the import directory entry */
static inline bool pe_get_import_dir(const IMAGE_NT_HEADERS *nt,
                                      IMAGE_DATA_DIRECTORY *out)
{
    return pe_get_data_dir(nt, IMAGE_DIRECTORY_ENTRY_IMPORT, out);
}

/* Convenience: get the base reloc directory entry */
static inline bool pe_get_basereloc_dir(const IMAGE_NT_HEADERS *nt,
                                         IMAGE_DATA_DIRECTORY *out)
{
    return pe_get_data_dir(nt, IMAGE_DIRECTORY_ENTRY_BASERELOC, out);
}

/* ── File header accessors ─────────────────────────────────────── */

static inline uint16_t pe_machine(const IMAGE_NT_HEADERS *nt)
{
    if (pe_is_pe32(nt))
        return nt->u.nt32.FileHeader.Machine;
    else
        return nt->u.nt64.FileHeader.Machine;
}

static inline uint32_t pe_pointer_to_symbol_table(const IMAGE_NT_HEADERS *nt)
{
    if (pe_is_pe32(nt))
        return nt->u.nt32.FileHeader.PointerToSymbolTable;
    else
        return nt->u.nt64.FileHeader.PointerToSymbolTable;
}

static inline uint32_t pe_number_of_symbols(const IMAGE_NT_HEADERS *nt)
{
    if (pe_is_pe32(nt))
        return nt->u.nt32.FileHeader.NumberOfSymbols;
    else
        return nt->u.nt64.FileHeader.NumberOfSymbols;
}

static inline uint16_t pe_characteristics(const IMAGE_NT_HEADERS *nt)
{
    if (pe_is_pe32(nt))
        return nt->u.nt32.FileHeader.Characteristics;
    else
        return nt->u.nt64.FileHeader.Characteristics;
}

static inline uint32_t pe_time_date_stamp(const IMAGE_NT_HEADERS *nt)
{
    if (pe_is_pe32(nt))
        return nt->u.nt32.FileHeader.TimeDateStamp;
    else
        return nt->u.nt64.FileHeader.TimeDateStamp;
}

/* ── RVA / Section helpers (PE32+PE64) ──────────────────────────── */

/* Convert RVA to file offset using the section table. Returns -1 if not mappable. */
static inline int rva_to_offset(const IMAGE_NT_HEADERS *nt, const IMAGE_SECTION_HEADER *sections,
                                uint32_t rva, size_t file_size)
{
    uint16_t num = pe_section_count(nt);
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
    if (rva < pe_size_of_headers(nt))
        return (int)rva;
    return -1;
}

/* Derive section table offset from DOS header and NT headers */
static inline int compute_section_table_offset(const IMAGE_DOS_HEADER *dos,
                                               const IMAGE_NT_HEADERS *nt)
{
    uint32_t pe_off = dos->e_lfanew;
    size_t sec_off = (size_t)pe_off + sizeof(uint32_t) + sizeof(IMAGE_FILE_HEADER) +
                     (size_t)pe_optional_header_size(nt);
    return (int)sec_off;
}

/*
 * get_image_sections — Locate the IMAGE_SECTION_HEADER array within a loaded PE image.
 *
 * Given the base address of the mapped PE image and its NT headers,
 * returns a pointer to the first section header, computed from the DOS
 * header offset and the OptionalHeader size.
 */
static inline IMAGE_SECTION_HEADER *get_image_sections(void *base, const IMAGE_NT_HEADERS *nt)
{
    const IMAGE_DOS_HEADER *dos = (const IMAGE_DOS_HEADER *)base;
    uint32_t pe_off = dos->e_lfanew;
    uint32_t sec_off = pe_off + sizeof(uint32_t) + sizeof(IMAGE_FILE_HEADER) +
                       pe_optional_header_size(nt);
    return (IMAGE_SECTION_HEADER *)((char *)base + sec_off);
}

#endif /* MY_WINE_PE_PRIV_H */
