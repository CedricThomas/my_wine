/*
 * dll_loader.c — DLL loading (map, relocate, register)
 *
 * Maps a DLL at a reserved base below 4GB, applies relocations,
 * registers in module list + LDR, and resolves its imports.
 * Glibc-free: all string/memory ops are hand-rolled or __builtin.
 * Suitable for calling from WINE_STUB context on guest stack.
 *
 * Extracted from import_resolve.c.
 */

#include "include/pe.h"
#include "include/pe_parser.h"
#include "include/common.h"
#include "include/nt_constants.h"
#include "loader_priv.h"
#include "include/debug.h"
#include "export_table.h"
#include "module_list.h"
#include "peb_ldr.h"
#include "../syscall/syscalls_inline.h"
#include "loader_utils.h"
#include "image_mapper.h"
#include "dll_path.h"
#include "dll_loader.h"

/* DLL base allocator: maps DLLs below 4GB to avoid GCC ms_abi truncation bug.
 * Uses atomic operations for allocation — still not fully thread-safe (mmap
 * and module registration are separate steps), but prevents overlapping bases.
 */
volatile uintptr_t g_dll_base_next = DLL_ALLOC_BASE;  /* Start at 1.5GB */

/**
 * load_dll: map a DLL, apply relocations, register in module list + LDR,
 * resolve its imports. Returns the loaded_module_t or NULL on failure.
 *
 * Glibc-free: all string/memory ops are hand-rolled or __builtin.
 * Suitable for calling from WINE_STUB context on guest stack. */
loaded_module_t *load_dll(const char *path, int depth)
{
    /* Save main PE globals — map_image_at overwrites them with the DLL's values */
    void *saved_image_base = g_image_base;
    char saved_pe_path[512];
    const char *cur_pe_path = get_pe_path();
    if (cur_pe_path) {
        size_t pe_len = 0;
        while (cur_pe_path[pe_len] && pe_len < sizeof(saved_pe_path) - 1)
            pe_len++;
        __builtin_memcpy(saved_pe_path, cur_pe_path, pe_len);
        saved_pe_path[pe_len] = '\0';
    } else {
        saved_pe_path[0] = '\0';
    }

    /* Atomically reserve a page-aligned base for this DLL using CAS loop.
     * This prevents two threads from mapping at the same address.
     * We reserve at least PAGE_SIZE upfront; the rest is advanced after mapping. */
    uintptr_t alloc_base;
    do {
        uintptr_t expected = __atomic_load_n(&g_dll_base_next, __ATOMIC_SEQ_CST);
        uintptr_t rounded = (expected + (PAGE_SIZE - 1)) & ~(uintptr_t)(PAGE_SIZE - 1);
        uintptr_t desired = rounded + PAGE_SIZE;  /* reserve minimum one page */
        if (__atomic_compare_exchange_n(&g_dll_base_next, &expected, desired,
                                        false, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST)) {
            alloc_base = rounded;
            break;
        }
    } while (1);

    /* Map the DLL at the reserved base below 4GB to avoid GCC ms_abi truncation */
    IMAGE_NT_HEADERS64 nt_copy;
    void *base = map_image_at(path, NULL, &nt_copy, NULL, alloc_base);

    /* Restore main PE globals (regardless of success/failure) */
    g_image_base = saved_image_base;
    set_pe_path(saved_pe_path);

    if (base == NULL) {
        DEBUG("  ERROR: map_image_at failed for '%s'", path);
        return NULL;
    }

    /* Advance g_dll_base_next past the actual DLL size.
     * We already reserved PAGE_SIZE atomically above, so only add the remainder. */
    uintptr_t dll_size = (nt_copy.OptionalHeader.SizeOfImage + (PAGE_SIZE - 1)) & ~(uintptr_t)(PAGE_SIZE - 1);
    if (dll_size > PAGE_SIZE) {
        __atomic_add_fetch(&g_dll_base_next, dll_size - PAGE_SIZE, __ATOMIC_SEQ_CST);
    }

    /* Extract NT headers from image memory */
    IMAGE_DOS_HEADER *img_dos = (IMAGE_DOS_HEADER *)base;
    IMAGE_NT_HEADERS64 *img_nt = (IMAGE_NT_HEADERS64 *)((char *)base + img_dos->e_lfanew);

    /* Extract DLL name from path (hand-rolled strrchr) */
    const char *name = path;
    const char *p = path;
    while (*p) {
        if (*p == '/') name = p + 1;
        p++;
    }

    /* Register in module list */
    loaded_module_t *mod = add_module(base, name, img_nt);
    if (mod == NULL) {
        DEBUG("  ERROR: module list full, cannot load '%s'", name);
        munmap(base, img_nt->OptionalHeader.SizeOfImage);
        return NULL;
    }

    /* Add to PEB LDR */
    if (g_peb_ldr != NULL) {
        ldr_add_module(mod);
    }

    /* Resolve this DLL's own imports (recursive).
     * resolve_module_imports also calls parse_export_table internally. */
    if (resolve_module_imports(mod, depth + 1) != 0) {
        DEBUG("  ERROR: import resolution failed for '%s'", name);
        /* Cleanup all resources allocated above */
        if (g_peb_ldr != NULL && mod->ldr_linked) {
            ldr_remove_module(mod);
        }
        reset_export_cache(mod);
        remove_module(mod);
        munmap(base, img_nt->OptionalHeader.SizeOfImage);
        return NULL;
    }

    /* Parse exports if not already done (e.g., DLL has no imports but has exports) */
    if (mod->export_cache.number_of_names == 0 &&
        img_nt->OptionalHeader.DataDirectory[DIRECTORY_ENTRY_EXPORT].VirtualAddress != 0) {
        parse_export_table(mod);
    }

    DEBUG("Loaded DLL: %s at %p", name, base);
    return mod;
}
