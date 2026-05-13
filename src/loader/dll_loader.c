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

#include <string.h>
#include <stdlib.h>
#include "include/pe.h"
#include "include/pe_parser.h"
#include "include/pe_priv.h"
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
 * The base tracker lives in g_loader.dll_base_next (volatile, atomic CAS).
 */

/**
 * load_dll: map a DLL, apply relocations, register in module list + LDR,
 * resolve its imports. Returns the loaded_module_t or NULL on failure.
 *
 * Glibc-free: all string/memory ops are hand-rolled or __builtin.
 * Suitable for calling from WINE_STUB context on guest stack. */
loaded_module_t *load_dll(const char *path, int depth)
{
    /* Save main PE globals — map_image_at overwrites them with the DLL's values */
    void *saved_image_base = g_loader.image_base;
    int saved_is_32bit = g_loader.is_32bit;
    char saved_pe_path[512];
    snprintf(saved_pe_path, sizeof(saved_pe_path), "%s", g_loader.pe_path);

    /* Atomically reserve a page-aligned base for this DLL using CAS loop.
     * This prevents two threads from mapping at the same address.
     * We reserve at least PAGE_SIZE upfront; the rest is advanced after mapping. */
    uintptr_t alloc_base;
    do {
        uintptr_t expected = __atomic_load_n(&g_loader.dll_base_next, __ATOMIC_SEQ_CST);
        uintptr_t rounded = (expected + (PAGE_SIZE - 1)) & ~(uintptr_t)(PAGE_SIZE - 1);
        uintptr_t desired = rounded + PAGE_SIZE;  /* reserve minimum one page */
        if (__atomic_compare_exchange_n(&g_loader.dll_base_next, &expected, desired,
                                        false, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST)) {
            alloc_base = rounded;
            break;
        }
    } while (1);

    /* Map the DLL at the reserved base below 4GB to avoid GCC ms_abi truncation */
    IMAGE_NT_HEADERS nt_copy;
    void *base = map_image_at(path, NULL, &nt_copy, NULL, alloc_base);

    /* Restore main PE globals (regardless of success/failure) */
    g_loader.image_base = saved_image_base;
    g_loader.is_32bit = saved_is_32bit;
    snprintf(g_loader.pe_path, sizeof(g_loader.pe_path), "%s", saved_pe_path);

    if (base == NULL) {
        DEBUG("  ERROR: map_image_at failed for '%s'", path);
        return NULL;
    }

    /* Advance g_loader.dll_base_next past the actual DLL size.
     * We already reserved PAGE_SIZE atomically above, so only add the remainder. */
    uintptr_t dll_size = (pe_size_of_image(&nt_copy) + (PAGE_SIZE - 1)) & ~(uintptr_t)(PAGE_SIZE - 1);
    if (dll_size > PAGE_SIZE) {
        __atomic_add_fetch(&g_loader.dll_base_next, dll_size - PAGE_SIZE, __ATOMIC_SEQ_CST);
    }

    /* Extract NT headers from image memory — reconstruct union from raw bytes */
    IMAGE_DOS_HEADER *img_dos = (IMAGE_DOS_HEADER *)base;
    uint32_t pe_off = img_dos->e_lfanew;

    /* Allocate NT headers on heap so they survive past this function */
    IMAGE_NT_HEADERS *img_nt = malloc(sizeof(IMAGE_NT_HEADERS));
    if (!img_nt) {
        INLINE_SYSCALL_MUNMAP(base, pe_size_of_image(&nt_copy));
        return NULL;
    }
    {
        uint32_t opt_off = pe_off + sizeof(uint32_t) + sizeof(IMAGE_FILE_HEADER);
        const uint16_t *magic = (const uint16_t *)((char *)base + opt_off);
        if (*magic == IMAGE_NT_OPTIONAL_HDR32_MAGIC) {
            img_nt->pe_type = PE_TYPE_32;
            memcpy(&img_nt->u.nt32, (char *)base + pe_off, sizeof(IMAGE_NT_HEADERS32));
        } else {
            img_nt->pe_type = PE_TYPE_64;
            memcpy(&img_nt->u.nt64, (char *)base + pe_off, sizeof(IMAGE_NT_HEADERS64));
        }
    }

    /* Check PE32/PE32+ mixing — DLL must match the main binary's PE type.
     * Use saved_is_32bit since map_image_at() overwrote g_is_32bit with the DLL's type. */
    if (saved_is_32bit && img_nt->pe_type == PE_TYPE_64) {
        DEBUG("  ERROR: cannot load PE32+ DLL '%s' for PE32 binary", path);
        free(img_nt);
        INLINE_SYSCALL_MUNMAP(base, pe_size_of_image(&nt_copy));
        return NULL;
    }
    if (!saved_is_32bit && img_nt->pe_type == PE_TYPE_32) {
        DEBUG("  ERROR: cannot load PE32 DLL '%s' for PE32+ binary", path);
        free(img_nt);
        INLINE_SYSCALL_MUNMAP(base, pe_size_of_image(&nt_copy));
        return NULL;
    }

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
        uintptr_t sz = pe_size_of_image(img_nt);
        free(img_nt);
        INLINE_SYSCALL_MUNMAP(base, sz);
        return NULL;
    }

    /* Add to PEB LDR */
    if (g_loader.peb_ldr != NULL) {
        ldr_add_module(mod);
    }

    /* Resolve this DLL's own imports (recursive).
     * resolve_module_imports also calls parse_export_table internally. */
    if (resolve_module_imports(mod, depth + 1) != 0) {
        DEBUG("  ERROR: import resolution failed for '%s'", name);
        /* Cleanup all resources allocated above */
        if (g_loader.peb_ldr != NULL && mod->ldr_linked) {
            ldr_remove_module(mod);
        }
        reset_export_cache(mod);
        remove_module(mod);
        uintptr_t sz = pe_size_of_image(img_nt);
        free(img_nt);
        INLINE_SYSCALL_MUNMAP(base, sz);
        return NULL;
    }

    /* Parse exports if not already done (e.g., DLL has no imports but has exports) */
    IMAGE_DATA_DIRECTORY exp_dir;
    if (mod->export_cache.number_of_names == 0 &&
        pe_get_data_dir(img_nt, DIRECTORY_ENTRY_EXPORT, &exp_dir) &&  /* check export dir */
        exp_dir.VirtualAddress != 0) {
        parse_export_table(mod);
    }

    DEBUG("Loaded DLL: %s at %p", name, base);
    return mod;
}
