/*
 * pe_headers.c — DOS/NT header parsing, section table, RVA conversion
 *
 * Parses DOS header, NT headers, sections, and provides debug dumping.
 * Shared helpers (safe_ptr_at, rva_to_offset, compute_section_table_offset) are in pe_priv.h.
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
#include "pe_priv.h"

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
                     IMAGE_NT_HEADERS64 *out_nt_headers)
{
    uint32_t pe_offset = dos_header->e_lfanew;

    /* Need at least the PE signature (4 bytes) */
    if (pe_offset + sizeof(uint32_t) > file_size)
        return -1;

    const uint32_t *sig_ptr = safe_ptr_at(base, pe_offset, sizeof(uint32_t), file_size);
    if (!sig_ptr || *sig_ptr != IMAGE_NT_SIGNATURE)
        return -1;

    /* Need full NT headers */
    if (pe_offset + sizeof(IMAGE_NT_HEADERS64) > file_size)
        return -1;

    const IMAGE_NT_HEADERS64 *nt = safe_ptr_at(base, pe_offset, sizeof(IMAGE_NT_HEADERS64), file_size);
    if (!nt)
        return -1;

    /* Validate machine type */
    if (nt->FileHeader.Machine != IMAGE_FILE_MACHINE_AMD64)
        return -1;

    /* Validate optional header magic (PE32+) */
    if (nt->OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR64_MAGIC)
        return -1;

    memcpy(out_nt_headers, nt, sizeof(IMAGE_NT_HEADERS64));
    return 0;
}

/* ── Sections ───────────────────────────────────────────────────── */

int parse_sections(const void *base, size_t file_size,
                   const IMAGE_NT_HEADERS64 *nt_headers,
                   IMAGE_SECTION_HEADER **out_sections)
{
    uint16_t num = nt_headers->FileHeader.NumberOfSections;
    size_t section_table_size = num * sizeof(IMAGE_SECTION_HEADER);

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

IMAGE_SECTION_HEADER *find_section_by_name(const IMAGE_NT_HEADERS64 *nt_headers,
                                            const IMAGE_SECTION_HEADER *sections,
                                            const char *name)
{
    uint16_t num = nt_headers->FileHeader.NumberOfSections;
    size_t name_len = strlen(name);
    if (name_len > 8)
        return NULL;

    for (uint16_t i = 0; i < num; i++) {
        /* Case-insensitive comparison of the 8-byte name field */
        if (strncasecmp((const char *)sections[i].Name, name, name_len) == 0 &&
            sections[i].Name[name_len] == '\0') {
            return (IMAGE_SECTION_HEADER *)&sections[i];
        }
    }
    return NULL;
}

/* ── Dump headers (debug) ───────────────────────────────────────── */

void dump_headers(const IMAGE_DOS_HEADER *dos, const IMAGE_NT_HEADERS64 *nt,
                  const IMAGE_SECTION_HEADER *sections)
{
    (void)dos;

    fprintf(stderr, "=== PE Header Dump ===\n");
    fprintf(stderr, "Machine:           0x%04x (%s)\n",
            nt->FileHeader.Machine,
            nt->FileHeader.Machine == IMAGE_FILE_MACHINE_AMD64 ? "AMD64" : "unknown");
    fprintf(stderr, "Entry point:       0x%08x\n", nt->OptionalHeader.AddressOfEntryPoint);
    fprintf(stderr, "Image base:        0x%016" PRIx64 "\n", nt->OptionalHeader.ImageBase);
    fprintf(stderr, "Section count:     %u\n", nt->FileHeader.NumberOfSections);
    fprintf(stderr, "\n");

    for (uint16_t i = 0; i < nt->FileHeader.NumberOfSections; i++) {
        const IMAGE_SECTION_HEADER *s = &sections[i];
        char name[9];
        memcpy(name, s->Name, 8);
        name[8] = '\0';

        fprintf(stderr, "Section %u:\n", i);
        fprintf(stderr, "  Name:             %s\n", name);
        fprintf(stderr, "  VirtualAddress:   0x%08x\n", s->VirtualAddress);
        fprintf(stderr, "  VirtualSize:      0x%08x\n", s->Misc.VirtualSize);
        fprintf(stderr, "  SizeOfRawData:    0x%08x\n", s->SizeOfRawData);
        fprintf(stderr, "  PointerToRawData: 0x%08x\n", s->PointerToRawData);
        fprintf(stderr, "  Characteristics:  0x%08x", s->Characteristics);

        /* Decode common characteristic flags */
        char flags[128] = "";
        if (s->Characteristics & IMAGE_SCN_MEM_READ)
            strcat(flags, " R");
        if (s->Characteristics & IMAGE_SCN_MEM_WRITE)
            strcat(flags, " W");
        if (s->Characteristics & IMAGE_SCN_MEM_EXECUTE)
            strcat(flags, " X");
        fprintf(stderr, "%s\n", flags);
        fprintf(stderr, "\n");
    }

    fprintf(stderr, "=====================\n");
}
