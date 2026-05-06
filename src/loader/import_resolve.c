/*
 * import_resolve.c — IAT resolution (pass 1 + pass 2 + thunk strategies)
 *
 * Resolves imports by patching IAT entries in the PE image.
 */

#define _GNU_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <search.h>
#include <strings.h>
#include <unistd.h>
#include <sys/mman.h>

#include "include/pe.h"
#include "include/pe_parser.h"
#include "include/common.h"
#include "loader_priv.h"
#include "include/debug.h"
#include "export_table.h"
#include "module_list.h"
#include "peb_ldr.h"

#define MAX_IMPORT_DEPTH 8

/* Forward declarations */
int find_dll_path(const char *dll_name, char *path, size_t path_size);
loaded_module_t *load_dll(const char *path, int depth);
static void init_exe_dir(void);

static char g_exe_dir[512] = {0};

/**
 * Initialize g_exe_dir with the current working directory (app directory).
 * Called once on first use. Matches Windows behavior where the app directory
 * is searched for DLLs.
 */
static void init_exe_dir(void)
{
    if (g_exe_dir[0] != '\0') return;
    if (getcwd(g_exe_dir, sizeof(g_exe_dir)) == NULL) {
        g_exe_dir[0] = '.';
        g_exe_dir[1] = '\0';
    }
}

/**
 * Find the .text jmp-thunk address whose IAT entry resolves to target_addr.
 * Scans all "ff 25 disp32" (jmp *disp(%rip)) instructions in .text and
 * checks if the dereferenced IAT pointer equals target_addr.
 * Returns the absolute address of the thunk instruction, or NULL.
 */
void *find_text_thunk(void *image_base, IMAGE_NT_HEADERS64 *nt,
                       IMAGE_SECTION_HEADER *sections,
                       void *target_addr)
{
    return find_rip_relative_jump_to(image_base, nt, sections,
                                      nt->FileHeader.NumberOfSections,
                                      target_addr);
}

static void *resolve_import(const char *dll_name, const char *func_name)
{
    /* Tier 1: lookup in our stub import table */
    size_t count = import_table_count;
    import_entry_t *entry = bsearch(func_name, import_table,
                                     count, sizeof(import_entry_t), import_cmp_by_name);
    if (entry != NULL && entry->address != NULL) {
        if (entry->dll_name && strcasecmp(entry->dll_name, dll_name) != 0) {
            fprintf(stderr, "  WARNING: %s found in %s but requested from %s\n",
                    func_name, entry->dll_name, dll_name);
        }
        return entry->address;
    }

    /* Tier 2: lookup in loaded module exports */
    loaded_module_t *mod = find_module_by_name(dll_name);
    if (mod != NULL && mod->export_cache != NULL) {
        void *addr = lookup_export(mod, func_name);
        if (addr != NULL) {
            DEBUG("    Resolved %s!%s from module %s via export table -> %p",
                  dll_name, func_name, mod->name, addr);
            return addr;
        }
    }

    /* Tier 3: not found */
    fprintf(stderr, "  ERROR: unresolved import: %s!%s\n", dll_name, func_name);
    return NULL;
}

/**
 * Pass 1: resolve import names and write to the descriptor's FirstThunk (IAT).
 */
static int resolve_import_pass1(void *base, IMAGE_NT_HEADERS64 *nt)
{
    IMAGE_OPTIONAL_HEADER64 *opt = &nt->OptionalHeader;

    if (opt->DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT].Size == 0) {
        DEBUG("No imports to resolve");
        return 0;
    }

    uint64_t import_rva = opt->DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT].VirtualAddress;
    IMAGE_IMPORT_DESCRIPTOR *desc = (IMAGE_IMPORT_DESCRIPTOR *)((char *)base + import_rva);

    DEBUG("Resolving imports:");

    while (desc->Name != 0) {
        const char *dll_name = (const char *)((char *)base + desc->Name);

        DEBUG("  DLL: %s", dll_name);

        IMAGE_THUNK_DATA64 *orig_thunks = (IMAGE_THUNK_DATA64 *)((char *)base + desc->u1.OriginalFirstThunk);
        IMAGE_THUNK_DATA64 *iath = (IMAGE_THUNK_DATA64 *)((char *)base + desc->FirstThunk);

        for (int i = 0; orig_thunks[i].AddressOfData != 0; i++) {
            void *addr = NULL;
            const char *func_name_for_debug = NULL;

            if (orig_thunks[i].AddressOfData & 0x8000000000000000ULL) {
                /* Ordinal import (high bit set) */
                uint16_t ordinal = (uint16_t)(orig_thunks[i].AddressOfData & 0xFFFF);
                const char *func_name = ordinal_lookup(dll_name, ordinal);
                if (func_name != NULL) {
                    addr = resolve_import(dll_name, func_name);
                    DEBUG("    Resolved ordinal %s!%d -> %s -> %p",
                          dll_name, ordinal, func_name, addr);
                } else {
                    fprintf(stderr, "  WARNING: ordinal import %s!%d not in lookup table\n",
                            dll_name, ordinal);
                }
                func_name_for_debug = "<ordinal>";
            } else {
                /* Name import */
                IMAGE_IMPORT_BY_NAME *imp_name = (IMAGE_IMPORT_BY_NAME *)((char *)base + orig_thunks[i].AddressOfData);
                func_name_for_debug = (const char *)imp_name->Name;
                addr = resolve_import(dll_name, func_name_for_debug);
            }

            if (addr != NULL) {
                fprintf(stderr, "    DBG_PASS1: %s!%s -> %p\n", dll_name, func_name_for_debug, addr);
                DEBUG("    Resolved %s -> %p", func_name_for_debug, addr);
                iath[i].AddressOfData = (uint64_t)(uintptr_t)addr;
            } else {
                fprintf(stderr, "    FAILED to resolve import at index %d\n", i);
            }
        }

        desc++;
    }

    return 0;
}

/**
 * Scan .text for "ff 25" (jmp *disp32(%rip)) instructions, collect and
 * deduplicate unique IAT target addresses, sort by address.
 * Returns the number of targets collected.
 */
static int collect_thunk_targets(void *base, IMAGE_NT_HEADERS64 *nt,
                                 uint64_t targets[MAX_THUNK_TARGETS])
{
    const IMAGE_DOS_HEADER *img_dos = (const IMAGE_DOS_HEADER *)base;
    uint32_t pe_off = img_dos->e_lfanew;
    uint32_t sec_off = pe_off + sizeof(uint32_t) + sizeof(IMAGE_FILE_HEADER) +
                       nt->FileHeader.SizeOfOptionalHeader;
    IMAGE_SECTION_HEADER *sections = (IMAGE_SECTION_HEADER *)((char *)base + sec_off);

    return scan_rip_relative_jumps(base, nt, sections,
                                    nt->FileHeader.NumberOfSections,
                                    targets, MAX_THUNK_TARGETS);
}

/**
 * Match thunk IAT targets against the flat import array and patch mismatches.
 * Uses four strategies: resolved-address overlap, ILT value match,
 * ILT offset/slot match, and positional fallback.
 */
static int patch_thunk_targets(void *base, IMAGE_NT_HEADERS64 *nt,
                               uint64_t *targets, int num_targets,
                               struct import_flat *flat, int num_flat)
{
    IMAGE_OPTIONAL_HEADER64 *opt = &nt->OptionalHeader;
    uint64_t import_dir_va = opt->DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT].VirtualAddress;
    uint64_t import_dir_end = opt->DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT].Size;

    int matched = 0;
    for (int t = 0; t < num_targets; t++) {
        uint64_t target = targets[t];
        uint64_t *target_ptr = (uint64_t *)((char *)base + target);
        uint64_t current_val = *target_ptr;

        int did_match = strategy_resolved_overlap(current_val, flat, num_flat) ||
            strategy_ilt_value_match(target_ptr, current_val, target, flat, num_flat) ||
            strategy_ilt_offset_match(target_ptr, target, current_val,
                                      import_dir_va, import_dir_end, flat, num_flat) ||
            strategy_positional(target_ptr, target, t, flat, num_flat);
        fprintf(stderr, "    DBG_PASS2: target=0x%lx val=0x%lx -> patched=%d final=0x%lx\n",
                (unsigned long)target, (unsigned long)current_val, did_match, (unsigned long)*target_ptr);
        if (did_match) {
            matched++;
        }
    }

    return matched;
}

/**
 * Pass 2: patch thunk IAT targets found by scanning .text
 * (for non-standard import layouts where .text jmp thunks reference
 *  addresses that differ from the descriptor's FirstThunk).
 */
static int resolve_import_pass2(void *base, IMAGE_NT_HEADERS64 *nt)
{
    IMAGE_OPTIONAL_HEADER64 *opt = &nt->OptionalHeader;

    if (opt->DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT].Size == 0) {
        return 0;
    }

    uint64_t thunk_targets[MAX_THUNK_TARGETS];
    int num_targets = collect_thunk_targets(base, nt, thunk_targets);

    if (num_targets == 0)
        return 0;

    DEBUG("  Found %d thunk targets in .text (range 0x%lx-0x%lx)",
          num_targets,
          (unsigned long)thunk_targets[0],
          (unsigned long)thunk_targets[num_targets - 1] + 7);

    struct import_flat flat[MAX_FLAT_IMPORTS];
    int num_flat = build_flat_import_array(base, nt, flat);

    DEBUG("  Flat import array: %d entries", num_flat);

    int matched = patch_thunk_targets(base, nt, thunk_targets, num_targets, flat, num_flat);

    DEBUG("  Thunk IAT patched: %d/%d targets resolved", matched, num_targets);

    return 0;
}

/**
 * Resolve all imports in the PE image.
 *
 * Pass 1: resolve and write to descriptor's FirstThunk (IAT).
 * Pass 2: patch thunk IAT targets found by scanning .text
 * (for non-standard import layouts where .text jmp thunks reference
 *  addresses that differ from the descriptor's FirstThunk).
 */
int resolve_imports(void *base, IMAGE_NT_HEADERS64 *nt)
{
    if (resolve_import_pass1(base, nt) != 0)
        return -1;
    return resolve_import_pass2(base, nt);
}

/**
 * Resolve imports for a dynamically loaded module.
 *
 * Handles recursive DLL dependencies: if the module imports from a DLL
 * that isn't loaded yet, load it first (up to MAX_IMPORT_DEPTH levels).
 *
 * @param mod  the loaded module to resolve imports for
 * @param depth  recursion depth (caller should pass 0)
 * @return 0 on success, -1 on failure
 */
int resolve_module_imports(loaded_module_t *mod, int depth)
{
    if (depth >= MAX_IMPORT_DEPTH) {
        fprintf(stderr, "  ERROR: import resolution depth exceeded (%d) for %s\n",
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
            /* Need to load this DLL — search in standard paths */
            char path[512];
            if (!find_dll_path(dll_name, path, sizeof(path))) {
                fprintf(stderr, "  ERROR: cannot find DLL '%s' imported by %s\n",
                        dll_name, mod->name);
                return -1;
            }

            /* Load the DLL (map + relocate + register) */
            dep = load_dll(path, depth + 1);
            if (dep == NULL) {
                fprintf(stderr, "  ERROR: failed to load '%s' for %s\n",
                        dll_name, mod->name);
                return -1;
            }
        }
        d++;
    }

    /* Second pass: resolve all imports using the three-tier resolver */
    if (resolve_imports(base, nt) != 0) {
        fprintf(stderr, "  ERROR: import resolution failed for %s\n", mod->name);
        return -1;
    }

    /* Parse exports so this module's functions can be found by others */
    mod->export_cache = parse_export_table(base, nt);

    return 0;
}

/* find_dll_path: search for a DLL in standard paths.
 * Search order: current dir, WINE_DLL_PATH env var.
 * Returns 1 if found (path filled), 0 if not found. */
int find_dll_path(const char *dll_name, char *path, size_t path_size)
{
    /* Try current directory */
    int ret = snprintf(path, path_size, "./%s", dll_name);
    if (ret >= 0 && (size_t)ret < path_size) {
        if (access(path, F_OK) == 0) return 1;
    }

    /* Try app directory (current working directory as app exe dir fallback) */
    init_exe_dir();
    if (g_exe_dir[0] != '.' || g_exe_dir[1] != '\0') {
        ret = snprintf(path, path_size, "%s/%s", g_exe_dir, dll_name);
        if (ret >= 0 && (size_t)ret < path_size) {
            if (access(path, F_OK) == 0) return 1;
        }
    }

    /* Try WINE_DLL_PATH */
    const char *wine_dll_path = getenv("WINE_DLL_PATH");
    if (wine_dll_path != NULL) {
        /* WINE_DLL_PATH is a semicolon-separated list */
        char *copy = strdup(wine_dll_path);
        if (copy != NULL) {
            char *tok = strtok(copy, ";");
            while (tok != NULL) {
                ret = snprintf(path, path_size, "%s/%s", tok, dll_name);
                if (ret >= 0 && (size_t)ret < path_size) {
                    if (access(path, F_OK) == 0) {
                        free(copy);
                        return 1;
                    }
                }
                tok = strtok(NULL, ";");
            }
            free(copy);
        }
    }

    return 0;
}

/* load_dll: map a DLL, apply relocations, register in module list + LDR, resolve its imports.
 * Returns the loaded_module_t or NULL on failure.
 *
 * Cleanup: if anything fails after add_module(), we undo all allocations
 * (LDR entry, export cache, module slot, mmap) to avoid resource leaks.
 */
loaded_module_t *load_dll(const char *path, int depth)
{
    /* Map the DLL */
    IMAGE_NT_HEADERS64 nt_copy;
    void *base = map_image(path, NULL, &nt_copy, NULL);
    if (base == NULL) {
        fprintf(stderr, "  ERROR: map_image failed for '%s'\n", path);
        return NULL;
    }

    /* Extract NT headers from image memory */
    IMAGE_DOS_HEADER *img_dos = (IMAGE_DOS_HEADER *)base;
    IMAGE_NT_HEADERS64 *img_nt = (IMAGE_NT_HEADERS64 *)((char *)base + img_dos->e_lfanew);

    /* Extract DLL name from path (basename) */
    const char *name = strrchr(path, '/');
    name = name ? name + 1 : path;

    /* Register in module list */
    loaded_module_t *mod = add_module(base, name, img_nt);
    if (mod == NULL) {
        fprintf(stderr, "  ERROR: module list full, cannot load '%s'\n", name);
        munmap(base, img_nt->OptionalHeader.SizeOfImage);
        return NULL;
    }

    /* Add to PEB LDR */
    if (g_peb_ldr != NULL) {
        ldr_add_module(mod);
    }

    /* Resolve this DLL's own imports (recursive).
     * resolve_module_imports also calls parse_export_table internally,
     * so we do NOT call it here — that would double-allocate export_cache. */
    if (resolve_module_imports(mod, depth + 1) != 0) {
        fprintf(stderr, "  ERROR: import resolution failed for '%s'\n", name);
        /* Cleanup all resources allocated above */
        if (g_peb_ldr != NULL) {
            ldr_remove_module(mod);
        }
        if (mod->export_cache != NULL) {
            free_export_cache(mod->export_cache);
            mod->export_cache = NULL;
        }
        remove_module(mod);
        munmap(base, img_nt->OptionalHeader.SizeOfImage);
        return NULL;
    }

    DEBUG("Loaded DLL: %s at %p", name, base);
    return mod;
}
