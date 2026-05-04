/*
 * pe_symbols.c — COFF symbol table I/O
 *
 * Parse COFF symbol tables directly from PE files on disk.
 * Shared by crt_refptrs.c, test_parse.c, and other modules.
 */

#define _GNU_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/stat.h>

#include "include/pe.h"
#include "include/pe_parser.h"

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
