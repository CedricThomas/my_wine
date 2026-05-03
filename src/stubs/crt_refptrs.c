/*
 * crt_refptrs.c — CRT refptr patching.
 *
 * Dynamically patches the PE's .refptr section entries to point to our
 * stub globals, so that CRT startup code doesn't crash on invalid
 * two-level indirection.
 */

#define _GNU_SOURCE

#include <stdio.h>
#include <sys/mman.h>
#include "msvcrt_priv.h"

/*
 * Name-to-target mappings for known CRT .refptr symbols.
 * The rel_offset is the offset from the .refptr section VirtualAddress,
 * derived from the original hardcoded RVAs (section base was 0x4300).
 * These offsets are linker-dependent but far more portable than absolute RVAs
 * since they work with any image base and section placement.
 */
const refptr_mapping_t refptr_mappings[] = {
    { "__CTOR_LIST__",              (void *)&ctor_list_stub,              0x000 },
    { "__DTOR_LIST__",              (void *)&dtor_list_stub,              0x010 },
    { "__xi_a",                     (void *)&xi_a_stub,                   0x020 },
    { "__dyn_tls_init_callback",    (void *)&dyn_tls_callback_stub,       0x030 },
    { "__image_base__",             (void *)&g_crt_ctx.image_base,        0x040 },
    { "__imp___initenv",            (void *)&__imp___initenv_stub,        0x050 },
    { "__mingw_oldexcpt_handler",   (void *)&mingw_excpt_handler_stub,    0x090 },
    { "__native_startup_lock",      (void *)&native_startup_lock,         0x0a0 },
    { "__native_startup_state",     (void *)&native_startup_state,        0x0b0 },
    { "__xc_a",                     (void *)&xc_a_stub,                   0x0c0 },
    { "__xc_z",                     (void *)&xc_z_stub,                   0x0d0 },
    { "__xi_a (dup)",               (void *)&xi_a_stub,                   0x0e0 },
    { "__xi_z",                     (void *)&xi_z_stub,                   0x0f0 },
    { "_commode",                   (void *)&_commode,                    0x100 },
    { "_dowildcard",                (void *)&dowildcard_val,              0x110 },
    { "_fmode",                     (void *)&_fmode,                      0x120 },
    { "_newmode",                   (void *)&newmode_val,                 0x150 },
    { "mingw_app_type",             (void *)&__msvcrt_app_type,           0x160 },
    { NULL, NULL, 0 }  /* terminator */
};

/* Helper: apply a single refptr patch with mprotect */
static void apply_refptr_patch(void *image_base, uint64_t rva, void *target,
                               const char *name, uint64_t image_size)
{
    if (rva >= image_size) {
        fprintf(stderr, "patch_crt_refptrs: %s rva 0x%lx >= image_size 0x%lx, skip\n",
                name, (unsigned long)rva, (unsigned long)image_size);
        return;
    }

    uint64_t *refptr = (uint64_t *)((char *)image_base + rva);
    void *old_val = (void *)*refptr;

    /* Make the containing page writable */
    char *page_start = (char *)((uint64_t)(char *)refptr & ~(uint64_t)4095);
    if (mprotect(page_start, 4096, PROT_READ | PROT_WRITE) != 0) {
        perror("patch_crt_refptrs: mprotect");
        return;
    }

    *refptr = (uint64_t)(uintptr_t)target;
    fprintf(stderr, "patch_crt_refptrs: %s at rva 0x%lx: 0x%lx -> %p (stub)\n",
            name, (unsigned long)rva, (unsigned long)old_val, target);

    /* Restore read-only.
     * If this fails, log with perror but don't abort — the page is still
     * writable which is suboptimal (leaves a writable page where we
     * intended read-only) but not fatal. The patch was already applied
     * successfully above. */
    if (mprotect(page_start, 4096, PROT_READ) != 0) {
        perror("patch_crt_refptrs: mprotect restore");
    }
}

void patch_crt_refptrs(void *image_base, IMAGE_NT_HEADERS64 *nt, IMAGE_SECTION_HEADER *sections)
{
    if (!image_base || !nt || !sections) return;

    /* Set g_crt_ctx.image_base to actual image base before applying patches */
    g_crt_ctx.image_base = (uint64_t)(uintptr_t)image_base;

    /* Dynamically find .bss section to set __imp___initenv_stub and g_crt_ctx.bss_vaddr */
    IMAGE_SECTION_HEADER *bss_sec = find_section_by_name(nt, sections, ".bss");
    if (bss_sec) {
        g_crt_ctx.bss_vaddr = bss_sec->VirtualAddress;
        /* Set __imp___initenv_stub to point to PE's envp in .bss
         * (the PE writes envp through this pointer) */
        __imp___initenv_stub = (void **)((char *)image_base + g_crt_ctx.bss_vaddr + 0x018);
    } else {
        fprintf(stderr, "patch_crt_refptrs: WARNING: .bss section not found\n");
        g_crt_ctx.bss_vaddr = 0;
    }

    /* __acrt_iob_func patching is done dynamically in the child process
     * (main.c:jump_to_entry) via find_text_thunk(). No need to patch here. */

    uint64_t image_size = nt->OptionalHeader.SizeOfImage;

    /* Find .refptr section dynamically */
    IMAGE_SECTION_HEADER *refptr_sec = find_section_by_name(nt, sections, ".refptr");
    if (!refptr_sec) {
        fprintf(stderr, "patch_crt_refptrs: WARNING: .refptr section not found, CRT refptr patching disabled\n");
        return;
    }

    uint64_t refptr_base = refptr_sec->VirtualAddress;
    uint64_t refptr_size = refptr_sec->Misc.VirtualSize;
    if (refptr_size == 0)
        refptr_size = refptr_sec->SizeOfRawData;

    fprintf(stderr, "patch_crt_refptrs: .refptr section at VA=0x%lx, size=0x%lx\n",
            (unsigned long)refptr_base, (unsigned long)refptr_size);

    /* Try COFF symbol table for dynamic name-based discovery */
    IMAGE_SYMBOL *symbols = NULL;
    char *string_table = NULL;
    int sym_count = parse_symbol_table_from_image(
        image_base, nt, nt->OptionalHeader.SizeOfHeaders,
        &symbols, &string_table);

    int used_symbol_table = 0;

    for (size_t i = 0; i < REF_MAP_COUNT; i++) {
        const refptr_mapping_t *map = &refptr_mappings[i];
        uint64_t target_rva;

        if (sym_count > 0 && symbols && string_table) {
            /* Try symbol table lookup first.
             * .refptr symbols appear as .refptr.<name> in the COFF table,
             * but the symbol name in the table is just <name> with
             * SectionNumber pointing to .refptr and Value being the
             * offset within the section. */
            uint32_t sym_value = lookup_symbol_value(symbols, sym_count,
                                                      string_table, map->name);
            if (sym_value != 0) {
                target_rva = refptr_base + sym_value;
                if (!used_symbol_table) {
                    fprintf(stderr, "patch_crt_refptrs: using COFF symbol table for discovery\n");
                    used_symbol_table = 1;
                }
            } else {
                /* Symbol not found in table — fall back to relative offset */
                target_rva = refptr_base + map->rel_offset;
            }
        } else {
            /* No symbol table — use fallback relative offsets */
            target_rva = refptr_base + map->rel_offset;
        }

        /* Validate the computed RVA is within the .refptr section */
        if (target_rva < refptr_base || target_rva + 8 > refptr_base + refptr_size) {
            fprintf(stderr, "patch_crt_refptrs: %s computed rva 0x%lx outside .refptr section, skipping\n",
                    map->name, (unsigned long)target_rva);
            continue;
        }

        apply_refptr_patch(image_base, target_rva, map->target,
                           map->name, image_size);
    }
}
