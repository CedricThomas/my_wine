/*
 * pe_parser.h — PE32+ binary parser API
 *
 * Functions for parsing and inspecting PE file structures.
 */

#ifndef MY_WINE_PE_PARSER_H
#define MY_WINE_PE_PARSER_H

#include "pe.h"

/* ── Header Parsing ──────────────────────────────────────────── */

/*
 * Parse the DOS header from the start of the file.
 * Returns 0 on success, -1 on error.
 */
int parse_dos_header(const void *base, size_t file_size,
                     IMAGE_DOS_HEADER *out_header);

/*
 * Parse the NT headers (PE signature + file header + optional header).
 * Returns 0 on success, -1 on error.
 */
int parse_nt_headers(const void *base, size_t file_size,
                     const IMAGE_DOS_HEADER *dos_header,
                     IMAGE_NT_HEADERS64 *out_nt_headers);

/*
 * Parse the section table.
 * Returns number of sections on success, -1 on error.
 * out_sections points into the file mapping (no allocation).
 */
int parse_sections(const void *base, size_t file_size,
                   const IMAGE_NT_HEADERS64 *nt_headers,
                   IMAGE_SECTION_HEADER **out_sections);

/*
 * Find a section by name (case-insensitive, up to 8 chars).
 * Returns NULL if not found.
 */
IMAGE_SECTION_HEADER *find_section_by_name(const IMAGE_NT_HEADERS64 *nt_headers,
                                            const IMAGE_SECTION_HEADER *sections,
                                            const char *name);

/*
 * Parse the import descriptor chain.
 * Returns number of import descriptors on success, -1 on error, 0 if none.
 * out_first_descriptor points into the file mapping.
 */
int parse_imports(const void *base, size_t file_size,
                  const IMAGE_NT_HEADERS64 *nt_headers,
                  IMAGE_IMPORT_DESCRIPTOR **out_first_descriptor);

/* ── Debug ───────────────────────────────────────────────────── */

void dump_headers(const IMAGE_DOS_HEADER *dos, const IMAGE_NT_HEADERS64 *nt,
                  const IMAGE_SECTION_HEADER *sections);

/* ── COFF Symbol Table ───────────────────────────────────────── */

/*
 * Parse the COFF symbol table from an already-loaded PE image.
 * The symbol table must be within the copied headers region.
 * Returns number of symbols parsed, or 0 if no symbol table / inaccessible.
 * The symbols and string_table pointers point directly into the image memory
 * (no allocation).
 */
int parse_symbol_table_from_image(void *image_base,
                                   const IMAGE_NT_HEADERS64 *nt_headers,
                                   size_t headers_size,
                                   IMAGE_SYMBOL **out_symbols,
                                   char **out_string_table);

/*
 * Parse the COFF symbol table directly from the PE file on disk.
 * Reads PointerToSymbolTable as a file offset, so it works even when
 * the symbol table lies beyond SizeOfHeaders.
 * Returns number of symbols, or 0 on failure.
 * The symbols and string_table are malloc'd; only symbols needs freeing.
 */
int parse_symbol_table_from_file(const char *path,
                                  const IMAGE_NT_HEADERS64 *nt_headers,
                                  IMAGE_SYMBOL **out_symbols,
                                  char **out_string_table);

/*
 * Get the name of a COFF symbol. Short names (8 bytes) or
 * long names from the string table. Returns NULL if unavailable.
 */
const char *get_symbol_name(const IMAGE_SYMBOL *sym, const char *string_table);

/*
 * Look up a symbol name and return its Value field.
 * Returns 0 if not found.
 */
uint32_t lookup_symbol_value(const IMAGE_SYMBOL *symbols, int count,
                             const char *string_table,
                             const char *name);

/*
 * Look up a symbol name and return its full RVA
 * (section VirtualAddress + symbol Value).
 * Returns 0 if not found or if the symbol has no valid section.
 */
uint32_t lookup_symbol_rva(const IMAGE_SYMBOL *symbols, int count,
                            const char *string_table,
                            const IMAGE_SECTION_HEADER *sections,
                            int num_sections,
                            const char *name);

#endif /* MY_WINE_PE_PARSER_H */
