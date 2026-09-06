/*
 * crt_priv.h — Internal CRT module struct definition
 *
 * Full definition of struct crt_module (opaque forward in crt.h).
 * Included by crt.c (registry) and each module implementation file.
 *
 * Only internal files should include this — never from public headers.
 */

#ifndef MY_WINE_CRT_PRIV_H
#define MY_WINE_CRT_PRIV_H

#include "include/crt.h"
#include "include/pe_parser.h"

struct crt_module {
    const char *name;
    crt_type_t type;

    // Detection — return non-zero if this module matches the PE
    int (*detect)(const char *file_path, IMAGE_NT_HEADERS *nt);

    // Entry symbols (NULL-terminated array)
    const char *const *entry_symbols;

    // Refptr mappings (NULL-terminated with {NULL,NULL} sentinel)
    const refptr_mapping_t *refptr_mappings;

    // BSS init offsets (0 = skip)
    uint32_t bss_init_offset;
    uint32_t bss_argv_offset;
    uint32_t bss_initenv_offset;

    // Vtable functions
    void (*patch_refptrs)(const char *file_path, void *image_base,
                          IMAGE_NT_HEADERS *nt, IMAGE_SECTION_HEADER *sections);
    void (*discover_offsets)(const char *file_path, IMAGE_NT_HEADERS *nt,
                             IMAGE_SECTION_HEADER *sections, crt_context_t *ctx);
    void (*seed_bss)(void *image_base, IMAGE_NT_HEADERS *nt,
                     IMAGE_SECTION_HEADER *sections);
};

#endif /* MY_WINE_CRT_PRIV_H */
