/*
 * crt.h — CRT module API
 *
 * Defines an opaque module interface for pluggable CRT implementations
 * (MinGW, Watcom, etc.) and the shared structures/types used across all
 * CRT backends.
 */

#ifndef MY_WINE_CRT_H
#define MY_WINE_CRT_H

#include <stdint.h>
#include <stddef.h>
#include "pe.h"

/* ── CRT type enumeration ──────────────────────────────────────── */

typedef enum {
    CRT_TYPE_MINGW,
    CRT_TYPE_WATCOM,
    CRT_TYPE_UNKNOWN
} crt_type_t;

/* ── CRT context (moved from include/msvcrt.h) ────────────────── */

typedef struct {
    uint64_t image_base;
    uint64_t bss_vaddr;
    uint32_t argc_bss_offset;
    uint32_t argv_bss_offset;
    uint32_t envp_bss_offset;
} crt_context_t;

/* ── Refptr mapping (moved from src/msvcrt/msvcrt_priv.h) ─────── */

typedef struct {
    const char *name;
    void       *target;
} refptr_mapping_t;

/* ── Opaque CRT module ─────────────────────────────────────────── */

typedef struct crt_module crt_module_t;

/* ── Accessor declarations ─────────────────────────────────────── */

/* Detect which CRT type a PE image was linked against */
crt_type_t crt_detect_type(const char *file_path, IMAGE_NT_HEADERS *nt);

/* Lookup a module by CRT type */
const crt_module_t *crt_get_module(crt_type_t type);

/* Get the null-terminated list of entry-point symbol names for the module */
const char *const *crt_entry_symbols(const crt_module_t *mod);

/* Get the refptr mapping table for the module (NULL-terminated) */
const refptr_mapping_t *crt_get_refptr_mappings(const crt_module_t *mod);

/* Get the number of refptr mappings in the module's table */
int crt_refptr_mapping_count(const crt_module_t *mod);

/* Patch the PE's refptrs using the module's mapping table */
void crt_patch_refptrs(const crt_module_t *mod, const char *file_path,
                       void *image_base, IMAGE_NT_HEADERS *nt,
                       IMAGE_SECTION_HEADER *sections);

/* Discover CRT-specific BSS offsets by scanning the PE */
void crt_discover_offsets(const crt_module_t *mod, const char *file_path,
                          IMAGE_NT_HEADERS *nt, IMAGE_SECTION_HEADER *sections,
                          crt_context_t *ctx);

/* Seed the BSS section with initial values (argc=0, argv=NULL, etc.) */
void crt_seed_bss(const crt_module_t *mod, void *image_base,
                  IMAGE_NT_HEADERS *nt, IMAGE_SECTION_HEADER *sections);

/* Get the default BSS offset for the init (argc) slot */
uint32_t crt_bss_init_offset(const crt_module_t *mod);

/* Get the default BSS offset for the argv slot */
uint32_t crt_bss_argv_offset(const crt_module_t *mod);

/* Get the default BSS offset for the initenv slot */
uint32_t crt_bss_initenv_offset(const crt_module_t *mod);

#endif /* MY_WINE_CRT_H */
