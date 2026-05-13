/*
 * crt.c — CRT module registry and dispatch
 *
 * Maintains the active CRT module and dispatches all CRT operations
 * (detection, refptr patching, BSS offset discovery, etc.) through
 * the vtable of the active module.
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>

#include "include/crt.h"
#include "include/pe_parser.h"

/* ── Internal module struct (backing the opaque forward in crt.h) ── */

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
    uint32_t bss_initenv_offset;

    // Vtable functions
    void (*patch_refptrs)(const char *file_path, void *image_base,
                          IMAGE_NT_HEADERS *nt, IMAGE_SECTION_HEADER *sections);
    void (*discover_offsets)(const char *file_path, IMAGE_NT_HEADERS *nt,
                             IMAGE_SECTION_HEADER *sections, crt_context_t *ctx);
    void (*seed_bss)(void *image_base, IMAGE_NT_HEADERS *nt,
                     IMAGE_SECTION_HEADER *sections);
};

/* ── Forward externs for module instances ─────────────────────── */

extern const crt_module_t crt_module_mingw;
extern const crt_module_t crt_module_watcom;

/* ── Module registry ──────────────────────────────────────────── */

static const crt_module_t *modules[] = {
    &crt_module_mingw,
    &crt_module_watcom,
    NULL
};

/* Active CRT module (set after detection) */
static const crt_module_t *active_crt = NULL;

/* ── Stub modules (full implementations in phase-specific files) ── */

/* Watcom CRT module stub (replaced by crt_watcom.c when implemented) */
const crt_module_t crt_module_watcom = {
    .name              = "watcom",
    .type              = CRT_TYPE_WATCOM,
    .detect            = NULL,
    .entry_symbols     = NULL,
    .refptr_mappings   = NULL,
    .bss_init_offset   = 0,
    .bss_initenv_offset = 0,
    .patch_refptrs     = NULL,
    .discover_offsets  = NULL,
    .seed_bss          = NULL,
};

/* ── Accessor implementations ─────────────────────────────────── */

crt_type_t crt_detect_type(const char *file_path, IMAGE_NT_HEADERS *nt)
{
    /*
     * Try each module's detect function. If none match, fall back to
     * a PE32/PE32+ heuristic:
     *   PE32 → try watcom (look for D_DoomMain symbol)
     *   PE32+ → assume mingw
     */
    for (int i = 0; modules[i]; i++) {
        if (modules[i]->detect && modules[i]->detect(file_path, nt)) {
            active_crt = modules[i];
            return modules[i]->type;
        }
    }

    // Heuristic fallback: PE32 → try watcom (look for D_DoomMain symbol)
    if (nt->pe_type == PE_TYPE_32) {
        IMAGE_SYMBOL *symbols = NULL;
        char *string_table = NULL;
        int count = parse_symbol_table_from_file(file_path, nt, &symbols, &string_table);
        if (count > 0) {
            uint32_t rva = lookup_symbol_value(symbols, count, string_table, "D_DoomMain");
            if (rva == 0) {
                rva = lookup_symbol_value(symbols, count, string_table, "_D_DoomMain");
            }
            if (rva != 0) {
                active_crt = &crt_module_watcom;
                /* Only free symbols — string_table is a pointer into the same
                 * combined malloc'd buffer (see parse_symbol_table_from_file). */
                free(symbols);
                return CRT_TYPE_WATCOM;
            }
            free(symbols);
        }
    }

    // Default to mingw
    active_crt = &crt_module_mingw;
    return CRT_TYPE_MINGW;
}

const crt_module_t *crt_get_module(crt_type_t type)
{
    for (int i = 0; modules[i]; i++) {
        if (modules[i]->type == type) {
            return modules[i];
        }
    }
    return NULL;
}

const char *const *crt_entry_symbols(const crt_module_t *mod)
{
    if (!mod) return NULL;
    return mod->entry_symbols;
}

const refptr_mapping_t *crt_get_refptr_mappings(const crt_module_t *mod)
{
    if (!mod) return NULL;
    return mod->refptr_mappings;
}

int crt_refptr_mapping_count(const crt_module_t *mod)
{
    if (!mod || !mod->refptr_mappings) return 0;

    int count = 0;
    const refptr_mapping_t *m = mod->refptr_mappings;
    while (m->name != NULL) {
        count++;
        m++;
    }
    return count;
}

void crt_patch_refptrs(const crt_module_t *mod, const char *file_path,
                       void *image_base, IMAGE_NT_HEADERS *nt,
                       IMAGE_SECTION_HEADER *sections)
{
    if (!mod || !mod->patch_refptrs) return;
    mod->patch_refptrs(file_path, image_base, nt, sections);
}

void crt_discover_offsets(const crt_module_t *mod, const char *file_path,
                          IMAGE_NT_HEADERS *nt, IMAGE_SECTION_HEADER *sections,
                          crt_context_t *ctx)
{
    if (!mod || !mod->discover_offsets) return;
    mod->discover_offsets(file_path, nt, sections, ctx);
}

void crt_seed_bss(const crt_module_t *mod, void *image_base,
                  IMAGE_NT_HEADERS *nt, IMAGE_SECTION_HEADER *sections)
{
    if (!mod || !mod->seed_bss) return;
    mod->seed_bss(image_base, nt, sections);
}

uint32_t crt_bss_init_offset(const crt_module_t *mod)
{
    if (!mod) return 0;
    return mod->bss_init_offset;
}

uint32_t crt_bss_initenv_offset(const crt_module_t *mod)
{
    if (!mod) return 0;
    return mod->bss_initenv_offset;
}
