/*
 * pe.c — PE32+ binary parser
 *
 * Reads and validates DOS header, NT headers, sections, and imports
 * from a PE file mapped into memory.
 */

#define _GNU_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <inttypes.h>
#include <strings.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/stat.h>

#include "include/pe.h"
#include "include/common.h"

/* ── Helpers ───────────────────────────────────────────────────── */

/* Return a pointer at a file offset, or NULL if offset + len exceeds file_size */
static const void *safe_ptr_at(const void *base, size_t offset, size_t len, size_t file_size)
{
    if (len > file_size || offset > file_size - len)
        return NULL;
    return (const uint8_t *)base + offset;
}

/* Convert RVA to file offset using the section table. Returns -1 if not mappable. */
static int rva_to_offset(const IMAGE_NT_HEADERS64 *nt, const IMAGE_SECTION_HEADER *sections,
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
static int compute_section_table_offset(const IMAGE_DOS_HEADER *dos,
                                        const IMAGE_NT_HEADERS64 *nt)
{
    uint32_t pe_off = dos->e_lfanew;
    size_t sec_off = (size_t)pe_off + sizeof(uint32_t) + sizeof(IMAGE_FILE_HEADER) +
                     (size_t)nt->FileHeader.SizeOfOptionalHeader;
    return (int)sec_off;
}

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

/* ── Parse imports ───────────────────────────────────────────────── */

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

/* ── COFF Symbol Table ───────────────────────────────────────────── */

/*
 * Parse the COFF symbol table directly from the PE file on disk.
 * Reads PointerToSymbolTable as a file offset (not image offset),
 * so it works even when the symbol table lies beyond SizeOfHeaders.
 *
 * The symbols and string_table are returned as malloc'd memory.
 * The string_table is embedded right after the symbols in the same buffer,
 * so only symbols needs to be freed (free(symbols) releases everything).
 *
 * Returns number of symbols, or 0 on failure.
 */
int parse_symbol_table_from_file(const char *path,
                                  const IMAGE_NT_HEADERS64 *nt_headers,
                                  IMAGE_SYMBOL **out_symbols,
                                  char **out_string_table)
{
    uint32_t ptr   = nt_headers->FileHeader.PointerToSymbolTable;
    uint32_t count = nt_headers->FileHeader.NumberOfSymbols;

    *out_symbols = NULL;
    *out_string_table = NULL;

    if (ptr == 0 || count == 0)
        return 0;

    size_t sym_table_size = (size_t)count * IMAGE_SIZEOF_SYMBOL;

    /* Open and mmap the file */
    int fd = open(path, O_RDONLY);
    if (fd < 0)
        return 0;

    struct stat st;
    if (fstat(fd, &st) < 0) {
        close(fd);
        return 0;
    }

    void *map = mmap(NULL, (size_t)st.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
    if (map == MAP_FAILED) {
        close(fd);
        return 0;
    }

    /* Validate symbol table is within file bounds */
    if (ptr + sym_table_size > (size_t)st.st_size) {
        munmap(map, (size_t)st.st_size);
        close(fd);
        return 0;
    }

    /* Read string table size (4 bytes right after the symbol table entries) */
    size_t str_off = ptr + sym_table_size;
    uint32_t str_data_size = 0;  /* size excluding the 4-byte length field */
    if (str_off + 4 <= (size_t)st.st_size) {
        str_data_size = *((const uint32_t *)((const uint8_t *)map + str_off));
        /* Clamp to actual file bounds — some linkers leave the length
         * field slightly larger than the real data on disk. */
        size_t max_available = (size_t)st.st_size - str_off - 4;
        if (str_data_size > max_available) {
            str_data_size = (uint32_t)max_available;
        }
    }

    /* Allocate combined buffer: symbols + 4-byte length + string data */
    size_t total_alloc = sym_table_size + 4 + (str_data_size > 0 ? str_data_size : 0);
    char *buf = (char *)malloc(total_alloc);
    if (!buf) {
        munmap(map, (size_t)st.st_size);
        close(fd);
        return 0;
    }

    /* Copy symbol table */
    memcpy(buf, (const uint8_t *)map + ptr, sym_table_size);

    /* Copy string table (length field + string data) */
    if (str_data_size > 0) {
        memcpy(buf + sym_table_size,
               (const uint8_t *)map + str_off,
               4 + str_data_size);
    } else {
        /* No string table — write zero length */
        *((uint32_t *)(buf + sym_table_size)) = 0;
    }

    *out_symbols = (IMAGE_SYMBOL *)buf;
    if (str_data_size > 0) {
        *out_string_table = buf + sym_table_size + 4;  /* skip the length prefix */
    }

    munmap(map, (size_t)st.st_size);
    close(fd);
    return (int)count;
}

/*
 * Get the name of a COFF symbol. Short names fit in 8 bytes;
 * long names are stored in the string table with a 4-byte offset prefix.
 */
const char *get_symbol_name(const IMAGE_SYMBOL *sym, const char *string_table)
{
    if (sym->N.ShortName[0] != 0) {
        /* Short name (up to 8 chars). Check if it's null-terminated within
         * the 8-byte field. If not, copy to a static buffer to avoid
         * reading past the buffer during string operations. */
        int has_null = 0;
        for (int j = 0; j < 8; j++) {
            if (sym->N.ShortName[j] == '\0') {
                has_null = 1;
                break;
            }
        }
        if (has_null) {
            return (const char *)sym->N.ShortName;
        }
        /* 8-byte name with no null — copy to static buffer for safety */
        static char short_name_buf[9];
        memcpy(short_name_buf, sym->N.ShortName, 8);
        short_name_buf[8] = '\0';
        return short_name_buf;
    }
    if (string_table) {
        uint32_t offset = sym->N.Name.Long;
        return (const char *)(string_table + offset);
    }
    return NULL;
}

/*
 * Look up a symbol name in the COFF symbol table and return its Value.
 * For .refptr-type symbols, SectionNumber > 0 and Value is the offset
 * within that section. For absolute symbols (SectionNumber == 0), Value
 * is the actual address/RVA.
 *
 * Returns 0 if not found.
 */
uint32_t lookup_symbol_value(const IMAGE_SYMBOL *symbols, int count,
                             const char *string_table,
                             const char *name)
{
    size_t name_len = strlen(name);
    if (name_len == 0)
        return 0;

    for (int i = 0; i < count; i++) {
        const char *sym_name = get_symbol_name(&symbols[i], string_table);
        if (sym_name && strncmp(sym_name, name, name_len) == 0 && sym_name[name_len] == '\0')
            return symbols[i].Value;
        /* Skip aux symbols that follow this entry */
        if (symbols[i].NumberOfAuxSymbols > 0) {
            i += symbols[i].NumberOfAuxSymbols;
            if (i >= count)
                break;
        }
    }
    return 0;
}

/*
 * Look up a symbol name in the COFF symbol table and return its full RVA
 * (section VirtualAddress + symbol Value). For the 'main' function, this
 * gives the address within the mapped image.
 *
 * Returns 0 if not found or if the symbol has no valid section.
 */
uint32_t lookup_symbol_rva(const IMAGE_SYMBOL *symbols, int count,
                            const char *string_table,
                            const IMAGE_SECTION_HEADER *sections,
                            int num_sections,
                            const char *name)
{
    size_t name_len = strlen(name);
    if (name_len == 0) return 0;

    for (int i = 0; i < count; i++) {
        const char *sym_name = get_symbol_name(&symbols[i], string_table);
        if (sym_name && strncmp(sym_name, name, name_len) == 0 && sym_name[name_len] == '\0') {
            int32_t sec_num = symbols[i].SectionNumber;
            if (sec_num > 0 && (uint16_t)sec_num <= (uint16_t)num_sections) {
                uint32_t rva = sections[sec_num - 1].VirtualAddress + symbols[i].Value;
                return rva;
            }
            /* Absolute symbol (sec==0) — Value is the address directly */
            if (sec_num == 0) {
                return symbols[i].Value;
            }
        }
        /* Skip aux symbols that follow this entry */
        if (symbols[i].NumberOfAuxSymbols > 0) {
            i += symbols[i].NumberOfAuxSymbols;
            if (i >= count) break;
        }
    }
    return 0;
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

/* ── Code Scanning ───────────────────────────────────────────────── */

int scan_rip_relative_jumps(void *image_base,
                            const IMAGE_NT_HEADERS64 *nt,
                            const IMAGE_SECTION_HEADER *sections,
                            int num_sections,
                            uint64_t *targets,
                            int max_targets)
{
    (void)num_sections;

    /* Find .text section */
    const IMAGE_SECTION_HEADER *text = find_section_by_name(nt, sections, ".text");
    if (text == NULL)
        return 0;

    uint64_t text_start = text->VirtualAddress;
    uint64_t text_end   = text_start + text->Misc.VirtualSize;
    if (text_end < text_start || text->Misc.VirtualSize > text->SizeOfRawData)
        text_end = text_start + text->SizeOfRawData;

    uint64_t text_size = text_end - text_start;

    /* Section too small for any valid instruction (need at least 6 bytes) */
    if (text_size < 6)
        return 0;

    uint8_t *text_base = (uint8_t *)image_base + text_start;
    int num_targets = 0;

    for (uint64_t off = 0; off + 6 <= text_size; off++) {
        if (text_base[off] == X86_JMP_RIP && text_base[off + 1] == X86_MOD_RIP) {
            int32_t disp = *(int32_t *)(text_base + off + 2);
            uint64_t instr_addr = text_start + off;
            uint64_t target = instr_addr + 6 + disp;

            /* Deduplicate */
            int dup = 0;
            for (int t = 0; t < num_targets; t++) {
                if (targets[t] == target) { dup = 1; break; }
            }
            if (!dup && num_targets < max_targets) {
                targets[num_targets++] = target;
            }
        }
    }

    /* Sort targets by address (insertion sort) */
    for (int i = 1; i < num_targets; i++) {
        uint64_t key = targets[i];
        int j = i - 1;
        while (j >= 0 && targets[j] > key) {
            targets[j + 1] = targets[j];
            j--;
        }
        targets[j + 1] = key;
    }

    return num_targets;
}

void *find_rip_relative_jump_to(void *image_base,
                                const IMAGE_NT_HEADERS64 *nt,
                                const IMAGE_SECTION_HEADER *sections,
                                int num_sections,
                                void *target_addr)
{
    /* Find .text section */
    const IMAGE_SECTION_HEADER *text = find_section_by_name(nt, sections, ".text");
    if (text == NULL)
        return NULL;

    uint64_t text_start = text->VirtualAddress;
    uint64_t text_end   = text_start + text->Misc.VirtualSize;
    if (text_end < text_start || text->Misc.VirtualSize > text->SizeOfRawData)
        text_end = text_start + text->SizeOfRawData;

    uint64_t text_size = text_end - text_start;

    /* Section too small for any valid instruction (need at least 6 bytes) */
    if (text_size < 6)
        return NULL;

    /* Compute image bounds from all sections for safe pointer dereference */
    uint64_t image_max = 0;
    for (int i = 0; i < num_sections; i++) {
        uint64_t s_end = sections[i].VirtualAddress + sections[i].Misc.VirtualSize;
        if (s_end > image_max)
            image_max = s_end;
    }

    uint8_t *text_base = (uint8_t *)image_base + text_start;
    uint64_t target_val = (uint64_t)(uintptr_t)target_addr;

    for (uint64_t off = 0; off + 6 <= text_size; off++) {
        if (text_base[off] == X86_JMP_RIP && text_base[off + 1] == X86_MOD_RIP) {
            int32_t disp = *(int32_t *)(text_base + off + 2);
            uint64_t instr_addr = text_start + off;
            uint64_t target_rva = instr_addr + 6 + disp;

            /* Bounds check: skip false matches that point outside the image */
            if (target_rva + 8 > image_max)
                continue;

            uint64_t *target_ptr = (uint64_t *)((char *)image_base + target_rva);
            if (*target_ptr == target_val) {
                return (void *)((char *)image_base + instr_addr);
            }
        }
    }
    return NULL;
}
