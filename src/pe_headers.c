/*
 * pe_headers.c — DOS/NT header parsing, section table, RVA conversion
 *
 * Parses DOS header, NT headers, sections, and provides debug dumping.
 * Shared helpers (safe_ptr_at, rva_to_offset, compute_section_table_offset) are in src/pe_priv.h.
 */

#define _GNU_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <inttypes.h>
#include <strings.h>

#include "include/pe.h"
#include "include/pe_parser.h"
#include "include/common.h"
#include "include/debug.h"
#include "src/pe_priv.h"

/* ── DOS Header ────────────────────────────────────────────────── */

int parse_dos_header(const void *base, size_t file_size, IMAGE_DOS_HEADER *out_header)
{
    if (file_size < sizeof(IMAGE_DOS_HEADER))
        return -1;

    const IMAGE_DOS_HEADER *hdr = safe_ptr_at(base, 0, sizeof(IMAGE_DOS_HEADER), file_size);
    if (!hdr)
        return -1;

    if (hdr->e_magic != IMAGE_DOS_SIGNATURE)
        return -1;

    memcpy(out_header, hdr, sizeof(IMAGE_DOS_HEADER));
    return 0;
}

/* ── NT Headers ────────────────────────────────────────────────── */

int parse_nt_headers(const void *base, size_t file_size,
                     const IMAGE_DOS_HEADER *dos_header,
                     IMAGE_NT_HEADERS *out_nt_headers)
{
    uint32_t pe_offset = dos_header->e_lfanew;

    /* Need at least the PE signature (4 bytes) */
    if (pe_offset + sizeof(uint32_t) > file_size)
        return -1;

    const uint32_t *sig_ptr = safe_ptr_at(base, pe_offset, sizeof(uint32_t), file_size);
    if (!sig_ptr || *sig_ptr != IMAGE_NT_SIGNATURE)
        return -1;

    /* Need at least the file header after the PE signature */
    uint32_t file_header_off = pe_offset + sizeof(uint32_t);
    if (file_header_off + sizeof(IMAGE_FILE_HEADER) > file_size)
        return -1;

    const IMAGE_FILE_HEADER *file_hdr = safe_ptr_at(base, file_header_off, sizeof(IMAGE_FILE_HEADER), file_size);
    if (!file_hdr)
        return -1;

    /* Validate machine type: must be I386 or AMD64 */
    if (file_hdr->Machine != IMAGE_FILE_MACHINE_AMD64 &&
        file_hdr->Machine != IMAGE_FILE_MACHINE_I386)
        return -1;

    /* Read OptionalHeader.Magic from raw bytes to determine PE type.
     * The Magic field is the first field (2 bytes) of the optional header. */
    uint32_t opt_header_off = file_header_off + sizeof(IMAGE_FILE_HEADER);
    if (opt_header_off + sizeof(uint16_t) > file_size)
        return -1;

    const uint16_t *magic_ptr = safe_ptr_at(base, opt_header_off, sizeof(uint16_t), file_size);
    if (!magic_ptr)
        return -1;

    uint16_t magic = *magic_ptr;

    if (magic == IMAGE_NT_OPTIONAL_HDR32_MAGIC) {
        /* PE32 */
        if (file_hdr->SizeOfOptionalHeader < sizeof(IMAGE_OPTIONAL_HEADER32))
            return -1;
        if (pe_offset > UINT32_MAX - sizeof(IMAGE_NT_HEADERS32) ||
            (size_t)pe_offset + sizeof(IMAGE_NT_HEADERS32) > file_size)
            return -1;

        const IMAGE_NT_HEADERS32 *nt = safe_ptr_at(base, pe_offset, sizeof(IMAGE_NT_HEADERS32), file_size);
        if (!nt)
            return -1;

        out_nt_headers->pe_type = PE_TYPE_32;
        memcpy(&out_nt_headers->u.nt32, nt, sizeof(IMAGE_NT_HEADERS32));
    } else if (magic == IMAGE_NT_OPTIONAL_HDR64_MAGIC) {
        /* PE32+ */
        if (file_hdr->SizeOfOptionalHeader < sizeof(IMAGE_OPTIONAL_HEADER64))
            return -1;
        if (pe_offset > UINT32_MAX - sizeof(IMAGE_NT_HEADERS64) ||
            (size_t)pe_offset + sizeof(IMAGE_NT_HEADERS64) > file_size)
            return -1;

        const IMAGE_NT_HEADERS64 *nt = safe_ptr_at(base, pe_offset, sizeof(IMAGE_NT_HEADERS64), file_size);
        if (!nt)
            return -1;

        out_nt_headers->pe_type = PE_TYPE_64;
        memcpy(&out_nt_headers->u.nt64, nt, sizeof(IMAGE_NT_HEADERS64));
    } else {
        return -1;
    }

    return 0;
}

/* ── Sections ───────────────────────────────────────────────────── */

int parse_sections(const void *base, size_t file_size,
                   const IMAGE_NT_HEADERS *nt_headers,
                   IMAGE_SECTION_HEADER **out_sections)
{
    uint16_t num = pe_section_count(nt_headers);
    size_t section_table_size = num * sizeof(IMAGE_SECTION_HEADER);
    if (num != 0 && section_table_size / sizeof(IMAGE_SECTION_HEADER) != num)
        return -1;

    /* Re-parse DOS header to get the PE offset for section table location */
    const IMAGE_DOS_HEADER *dos = safe_ptr_at(base, 0, sizeof(IMAGE_DOS_HEADER), file_size);
    if (!dos || dos->e_magic != IMAGE_DOS_SIGNATURE)
        return -1;

    int sec_table_off = compute_section_table_offset(dos, nt_headers);
    if (sec_table_off < 0 || (size_t)sec_table_off + section_table_size > file_size)
        return -1;

    const IMAGE_SECTION_HEADER *sec = safe_ptr_at(base, (size_t)sec_table_off, section_table_size, file_size);
    if (!sec)
        return -1;

    /* Validate each section's raw data region is within file bounds */
    for (uint16_t i = 0; i < num; i++) {
        const IMAGE_SECTION_HEADER *s = &sec[i];
        if (s->PointerToRawData != 0) {
            if ((size_t)s->PointerToRawData + (size_t)s->SizeOfRawData > file_size)
                return -1;
        }
    }

    /* Sections are embedded in the binary — no allocation needed. */
    *out_sections = (IMAGE_SECTION_HEADER *)sec;
    return (int)num;
}

/* ── Find section by name ───────────────────────────────────────── */

/* In the 32-bit standalone build, musl's strncasecmp is an ifunc whose PLT
 * resolver returns without executing the actual comparison (same bug as
 * strncpy). Use our hand-rolled _m_strncasecmp from crt_32_stub.c instead. */
#ifdef MY_WINE32
extern int _m_strncasecmp(const char *a, const char *b, size_t n);
#endif

const IMAGE_SECTION_HEADER *find_section_by_name(const IMAGE_NT_HEADERS *nt_headers,
                                                  const IMAGE_SECTION_HEADER *sections,
                                                  const char *name)
{
    uint16_t num = pe_section_count(nt_headers);
    size_t name_len = strlen(name);
    if (name_len > 8)
        return NULL;

    for (uint16_t i = 0; i < num; i++) {
        /* Case-insensitive comparison of the 8-byte name field */
#ifdef MY_WINE32
        int cmp = _m_strncasecmp((const char *)sections[i].Name, name, name_len);
#else
        int cmp = strncasecmp((const char *)sections[i].Name, name, name_len);
#endif
        if (cmp == 0 && sections[i].Name[name_len] == '\0') {
            return (const IMAGE_SECTION_HEADER *)&sections[i];
        }
    }
    DEBUG_LEVEL(3, "find_section_by_name: not found '%s' (searched %d sections)", name, num);
    return NULL;
}

/* ── Find code section ────────────────────────────────────────── */

/*
 * Find the primary code section. Tries name lookup in priority order:
 *   .text, BEGTEXT, TEXT, CODE (case-insensitive)
 * Falls back to scanning for Characteristics with both
 * IMAGE_SCN_CNT_CODE and IMAGE_SCN_MEM_EXECUTE set.
 * Returns NULL if no code section found.
 */

IMAGE_SECTION_HEADER *find_code_section(const IMAGE_NT_HEADERS *nt_headers,
                                         const IMAGE_SECTION_HEADER *sections)
{
    uint16_t num = pe_section_count(nt_headers);
    if (num == 0)
        return NULL;

    /* Priority-ordered name lookup (case-insensitive) */
    static const char *const code_names[] = { ".text", "BEGTEXT", "TEXT", "CODE" };
    for (size_t n = 0; n < sizeof(code_names) / sizeof(code_names[0]); n++) {
        size_t name_len = strlen(code_names[n]);
        for (uint16_t i = 0; i < num; i++) {
#ifdef MY_WINE32
            int cmp = _m_strncasecmp((const char *)sections[i].Name, code_names[n], name_len);
#else
            int cmp = strncasecmp((const char *)sections[i].Name, code_names[n], name_len);
#endif
            if (cmp == 0 && sections[i].Name[name_len] == '\0') {
                return (IMAGE_SECTION_HEADER *)&sections[i];
            }
        }
    }

    /* Fallback: first section with both code and execute flags */
    for (uint16_t i = 0; i < num; i++) {
        if ((sections[i].Characteristics & IMAGE_SCN_CNT_CODE) &&
            (sections[i].Characteristics & IMAGE_SCN_MEM_EXECUTE)) {
            return (IMAGE_SECTION_HEADER *)&sections[i];
        }
    }

    return NULL;
}

/* ── Dump headers (debug) ───────────────────────────────────────── */

void dump_headers(const IMAGE_DOS_HEADER *dos, const IMAGE_NT_HEADERS *nt,
                  const IMAGE_SECTION_HEADER *sections)
{
    (void)dos;

    DEBUG_LEVEL(2, "=== PE Header Dump ===");
    uint16_t machine = pe_machine(nt);
    DEBUG_LEVEL(2, "Machine:           0x%04x (%s)",
            machine,
            machine == IMAGE_FILE_MACHINE_AMD64 ? "AMD64" :
            machine == IMAGE_FILE_MACHINE_I386 ? "I386" : "unknown");
    DEBUG_LEVEL(2, "PE type:           %s",
            pe_is_pe32(nt) ? "PE32" : "PE32+");
    DEBUG_LEVEL(2, "Entry point:       0x%08x", pe_entry_rva(nt));
    DEBUG_LEVEL(2, "Image base:        0x%016" PRIx64, pe_image_base(nt));
    DEBUG_LEVEL(2, "Section count:     %u", pe_section_count(nt));
    DEBUG_LEVEL(2, "");

    for (uint16_t i = 0; i < pe_section_count(nt); i++) {
        const IMAGE_SECTION_HEADER *s = &sections[i];
        char name[9];
        memcpy(name, s->Name, 8);
        name[8] = '\0';

        DEBUG_LEVEL(2, "Section %u:", i);
        DEBUG_LEVEL(2, "  Name:             %s", name);
        DEBUG_LEVEL(2, "  VirtualAddress:   0x%08x", s->VirtualAddress);
        DEBUG_LEVEL(2, "  VirtualSize:      0x%08x", s->Misc.VirtualSize);
        DEBUG_LEVEL(2, "  SizeOfRawData:    0x%08x", s->SizeOfRawData);
        DEBUG_LEVEL(2, "  PointerToRawData: 0x%08x", s->PointerToRawData);
        DEBUG_LEVEL(2, "  Characteristics:  0x%08x", s->Characteristics);

        /* Decode common characteristic flags */
        char flags[128] = "";
        if (s->Characteristics & IMAGE_SCN_MEM_READ)
            strcat(flags, " R");
        if (s->Characteristics & IMAGE_SCN_MEM_WRITE)
            strcat(flags, " W");
        if (s->Characteristics & IMAGE_SCN_MEM_EXECUTE)
            strcat(flags, " X");
        DEBUG_LEVEL(2, "%s", flags);
        DEBUG_LEVEL(2, "");
    }

    DEBUG_LEVEL(2, "=====================");
}
