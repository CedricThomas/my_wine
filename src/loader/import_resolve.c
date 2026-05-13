/*
 * import_resolve.c — IAT resolution (pass 1 + pass 2 + thunk strategies)
 *
 * Resolves imports by patching IAT entries in the PE image.
 * Glibc-free: all string/memory ops are hand-rolled.
 * Binary search in import table is hand-rolled (no bsearch dependency).
 */

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
#include "dll_path.h"
#include "dll_loader.h"

#define MAX_IMPORT_DEPTH 8

/**
 * Find the .text jmp-thunk address whose IAT entry resolves to target_addr.
 */
void *find_text_thunk(void *image_base, IMAGE_NT_HEADERS *nt,
                       IMAGE_SECTION_HEADER *sections,
                       void *target_addr)
{
    return find_rip_relative_jump_to(image_base, nt, sections,
                                      pe_section_count(nt),
                                      target_addr);
}

static void *resolve_import(const char *dll_name, const char *func_name)
{
    /* Tier 1: lookup in our stub import table (hand-rolled binary search) */
    size_t lo = 0, hi = import_table_count;
    import_entry_t *entry = NULL;
    while (lo < hi) {
        size_t mid = lo + (hi - lo) / 2;
        int cmp = import_cmp_by_name(func_name, &import_table[mid]);
        if (cmp < 0) {
            hi = mid;
        } else if (cmp > 0) {
            lo = mid + 1;
        } else {
            entry = &import_table[mid];
            break;
        }
    }
    if (entry != NULL && entry->address != NULL) {
        if (entry->dll_name && dll_strcasecmp(entry->dll_name, dll_name) != 0) {
            DEBUG("  WARNING: %s found in %s but requested from %s",
                  func_name, entry->dll_name, dll_name);
        }
        return entry->address;
    }

    /* Tier 2: lookup in loaded module exports */
    loaded_module_t *mod = find_module_by_name(dll_name);
    if (mod != NULL && mod->export_cache.number_of_names > 0) {
        void *addr = lookup_export(mod, func_name);
        if (addr != NULL) {
            DEBUG("    Resolved %s!%s from module %s via export table -> %p",
                  dll_name, func_name, mod->name, addr);
            return addr;
        }
    }

    /* Tier 3: not found */
    DEBUG("  ERROR: unresolved import: %s!%s", dll_name, func_name);
    return NULL;
}

/**
 * Pass 1: resolve import names and write to the descriptor's FirstThunk (IAT).
 */
static int resolve_import_pass1(void *base, IMAGE_NT_HEADERS *nt)
{
    IMAGE_DATA_DIRECTORY imp_dir;
    if (!pe_get_import_dir(nt, &imp_dir)) {
        DEBUG("No imports to resolve");
        return 0;
    }
    if (imp_dir.Size == 0) {
        DEBUG("No imports to resolve");
        return 0;
    }

    uint64_t import_rva = imp_dir.VirtualAddress;
    IMAGE_IMPORT_DESCRIPTOR *desc = (IMAGE_IMPORT_DESCRIPTOR *)((char *)base + import_rva);

    bool is32 = pe_is_pe32(nt);
    uint64_t high_bit_mask = is32 ? 0x80000000 : 0x8000000000000000ULL;
    size_t thunk_size = is32 ? sizeof(uint32_t) : sizeof(uint64_t);

    DEBUG("Resolving imports:");

    while (desc->Name != 0) {
        const char *dll_name = (const char *)((char *)base + desc->Name);

        DEBUG("  DLL: %s", dll_name);

        uint8_t *orig_base = (uint8_t *)((char *)base + desc->u1.OriginalFirstThunk);
        uint8_t *iat_base  = (uint8_t *)((char *)base + desc->FirstThunk);
        int i;

        for (i = 0; ; i++) {
            uint64_t thunk_val;
            if (is32) {
                thunk_val = (uint32_t)*((uint32_t *)(orig_base + i * thunk_size));
            } else {
                thunk_val = *((uint64_t *)(orig_base + i * thunk_size));
            }
            if (thunk_val == 0) break;

            void *addr = NULL;
            const char *func_name_for_debug = NULL;

            if (thunk_val & high_bit_mask) {
                /* Ordinal import (high bit set) */
                uint16_t ordinal = (uint16_t)(thunk_val & 0xFFFF);
                const char *func_name = ordinal_lookup(dll_name, ordinal);
                if (func_name != NULL) {
                    addr = resolve_import(dll_name, func_name);
                    DEBUG("    Resolved ordinal %s!%d -> %s -> %p",
                          dll_name, ordinal, func_name, addr);
                } else {
                    DEBUG("  WARNING: ordinal import %s!%d not in lookup table",
                          dll_name, ordinal);
                }
                func_name_for_debug = "<ordinal>";
            } else {
                /* Name import */
                IMAGE_IMPORT_BY_NAME *imp_name = (IMAGE_IMPORT_BY_NAME *)((char *)base + thunk_val);
                func_name_for_debug = (const char *)imp_name->Name;
                addr = resolve_import(dll_name, func_name_for_debug);
            }

            if (addr != NULL) {
                DEBUG("    Resolved %s -> %p", func_name_for_debug, addr);
                if (is32) {
                    *(uint32_t *)(iat_base + i * thunk_size) = (uint32_t)(uintptr_t)addr;
                } else {
                    *(uint64_t *)(iat_base + i * thunk_size) = (uint64_t)(uintptr_t)addr;
                }
            } else {
                DEBUG("    FAILED to resolve import at index %d", i);
            }
        }

        desc++;
    }

    return 0;
}

/**
 * Scan .text for "ff 25" (jmp *disp32(%rip)) instructions, collect and
 * deduplicate unique IAT target addresses, sort by address.
 */
static int collect_thunk_targets(void *base, IMAGE_NT_HEADERS *nt,
                                 uint64_t targets[MAX_THUNK_TARGETS])
{
    IMAGE_SECTION_HEADER *sections = get_image_sections(base, nt);

    return scan_rip_relative_jumps(base, nt, sections,
                                    pe_section_count(nt),
                                    targets, MAX_THUNK_TARGETS);
}

/**
 * Match thunk IAT targets against the flat import array and patch mismatches.
 */
static int patch_thunk_targets(void *base, IMAGE_NT_HEADERS *nt,
                               uint64_t *targets, int num_targets,
                               struct import_flat *flat, int num_flat)
{
    bool is32 = pe_is_pe32(nt);
    size_t thunk_size = is32 ? sizeof(uint32_t) : sizeof(uint64_t);
    int matched = 0;
    for (int t = 0; t < num_targets; t++) {
        uint64_t target = targets[t];
        void *target_ptr = (char *)base + target;
        uint64_t current_val;
        if (is32) {
            current_val = (uint32_t)*((uint32_t *)target_ptr);
        } else {
            current_val = *((uint64_t *)target_ptr);
        }

        int did_match = strategy_resolved_overlap(current_val, target_ptr, flat, num_flat) ||
            strategy_ilt_value_match(target_ptr, current_val, target, thunk_size,
                                     flat, num_flat) ||
            strategy_ilt_offset_match(target_ptr, target, current_val, thunk_size,
                                      flat, num_flat);
        if (did_match) {
            matched++;
        } else {
            DEBUG("    Thunk patch UNMATCHED at 0x%lx (current=0x%lx)",
                  (unsigned long)target, (unsigned long)current_val);
        }
    }

    return matched;
}

/**
 * Pass 2: patch thunk IAT targets found by scanning .text
 */
static int resolve_import_pass2(void *base, IMAGE_NT_HEADERS *nt)
{
    IMAGE_DATA_DIRECTORY imp_dir;
    if (!pe_get_import_dir(nt, &imp_dir)) {
        return 0;
    }
    if (imp_dir.Size == 0) {
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
 */
int resolve_imports(void *base, IMAGE_NT_HEADERS *nt)
{
    if (resolve_import_pass1(base, nt) != 0)
        return -1;
    return resolve_import_pass2(base, nt);
}

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
    IMAGE_NT_HEADERS *nt = mod->nt;

    IMAGE_DATA_DIRECTORY imp_dir;
    if (!pe_get_import_dir(nt, &imp_dir)) {
        return 0; /* No imports */
    }
    if (imp_dir.Size == 0) {
        return 0; /* No imports */
    }

    uint64_t import_rva = imp_dir.VirtualAddress;
    IMAGE_IMPORT_DESCRIPTOR *desc = (IMAGE_IMPORT_DESCRIPTOR *)((char *)base + import_rva);

    /* First pass: ensure all dependency DLLs are loaded */
    IMAGE_IMPORT_DESCRIPTOR *d = desc;
    while (d->Name != 0) {
        const char *dll_name = (const char *)((char *)base + d->Name);

        /* Check if already loaded */
        loaded_module_t *dep = find_module_by_name(dll_name);
        if (dep == NULL) {
            /* Check if this is a known stub library */
            if (dll_strcasecmp("kernel32.dll", dll_name) == 0 ||
                dll_strcasecmp("ntdll.dll", dll_name) == 0 ||
                dll_strcasecmp("msvcrt.dll", dll_name) == 0) {
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


