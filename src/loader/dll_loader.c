/*
 * dll_loader.c — DLL loading and module import resolution
 *
 * Handles dynamic DLL loading: mapping at a safe base, registering in the
 * module list and PEB LDR, and resolving imports recursively.
 *
 * Glibc-free: all string/memory ops are hand-rolled or __builtin.
 * Suitable for calling from WINE_STUB context on guest stack.
 *
 * Extracted from import_resolve.c.
 */

#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/mman.h>

#include "include/pe.h"
#include "include/pe_parser.h"
#include "include/common.h"
#include "include/nt_constants.h"
#include "include/debug.h"
#include "loader_utils.h"
#include "dll_path.h"
#include "export_table.h"
#include "module_list.h"
#include "peb_ldr.h"
#include "../syscall/syscalls_inline.h"
#include "image_mapper.h"
#include "import_resolve.h"
#include "dll_loader.h"

#define MAX_IMPORT_DEPTH 8

#define DLL_ALLOC_BASE 0x60000000  /* DLL base allocator: maps DLLs below 4GB to avoid GCC ms_abi truncation bug */
/* DLL base allocator: maps DLLs below 4GB to avoid GCC ms_abi truncation bug.
 * Uses atomic operations for allocation — still not fully thread-safe (mmap
 * and module registration are separate steps), but prevents overlapping bases.
 */
static uintptr_t g_dll_base_next = DLL_ALLOC_BASE;  /* Start at 1.5GB */

/* Case-insensitive string equality */
static int strci_equal(const char *a, const char *b)
{
    return dll_strcasecmp(a, b) == 0;
}

/* Forward declarations */
int find_dll_path(const char *dll_name, char *path, size_t path_size);

/**
 * Resolve imports for a dynamically loaded module.
 */
int resolve_module_imports(loaded_module_t *mod, int depth)
{
    if (depth >= MAX_IMPORT_DEPTH) {
        DEBUG("  ERROR: import resolution depth exceeded (%d) for %s",
              MAX_IMPORT_DEPTH, mod->name);
        return -1;
    }

    void *base = mod->base;
    IMAGE_NT_HEADERS64 *nt = mod->nt;

    IMAGE_OPTIONAL_HEADER64 *opt = &nt->OptionalHeader;
    if (opt->DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT].Size == 0) {
        return 0; /* No imports */
    }

    uint64_t import_rva = opt->DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT].VirtualAddress;
    IMAGE_IMPORT_DESCRIPTOR *desc = (IMAGE_IMPORT_DESCRIPTOR *)((char *)base + import_rva);

    /* First pass: ensure all dependency DLLs are loaded */
    IMAGE_IMPORT_DESCRIPTOR *d = desc;
    while (d->Name != 0) {
        const char *dll_name = (const char *)((char *)base + d->Name);

        /* Check if already loaded */
        loaded_module_t *dep = find_module_by_name(dll_name);
        if (dep == NULL) {
            /* Check if this is a known stub library */
            if (strci_equal("kernel32.dll", dll_name) ||
                strci_equal("ntdll.dll", dll_name) ||
                strci_equal("msvcrt.dll", dll_name)) {
                DEBUG("  Skipping stub library '%s' for %s (resolved via import table)",
                      dll_name, mod->name);
                d++;
                continue;
            }

            /* Need to load this DLL */
            char path[512];
            if (!find_dll_path(dll_name, path, sizeof(path))) {
                DEBUG("  ERROR: cannot find DLL '%s' imported by %s",
                      dll_name, mod->name);
                return -1;
            }

            /* Load the DLL (map + relocate + register) */
            dep = load_dll(path, depth + 1);
            if (dep == NULL) {
                DEBUG("  ERROR: failed to load '%s' for %s",
                      dll_name, mod->name);
                return -1;
            }
        }
        d++;
    }

    /* Second pass: resolve all imports using the three-tier resolver */
    if (resolve_imports(base, nt) != 0) {
        DEBUG("  ERROR: import resolution failed for %s", mod->name);
        return -1;
    }

    /* Parse exports so this module's functions can be found by others */
    if (parse_export_table(mod) != 0) {
        /* parse_export_table returns -1 if no export dir — that's OK */
        DEBUG("  parse_export_table returned -1 for %s (no exports?)", mod->name);
    }

    return 0;
}

/* load_dll: map a DLL, apply relocations, register in module list + LDR,
 * resolve its imports. Returns the loaded_module_t or NULL on failure.
 *
 * Glibc-free: all string/memory ops are hand-rolled or __builtin.
 * Suitable for calling from WINE_STUB context on guest stack.
 */
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
