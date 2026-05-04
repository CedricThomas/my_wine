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

#include "include/pe.h"
#include "include/pe_parser.h"
#include "include/common.h"
#include "loader_priv.h"
#include "include/debug.h"

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
    size_t count = import_table_count;
    import_entry_t *entry = bsearch(func_name, import_table,
                                     count, sizeof(import_entry_t), import_cmp_by_name);
    if (entry == NULL) {
        fprintf(stderr, "  ERROR: unresolved import: %s!%s\n", dll_name, func_name);
        return NULL;
    }
    if (entry->address == NULL) {
        fprintf(stderr, "  ERROR: import %s!%s has NULL address (not initialized)\n",
                dll_name, func_name);
        return NULL;
    }
    /* DLL name mismatch: warn but still resolve (same function
     * may be exported from multiple DLLs by the PE compiler) */
    if (entry->dll_name && strcasecmp(entry->dll_name, dll_name) != 0) {
        fprintf(stderr, "  WARNING: %s found in %s but requested from %s (resolving anyway)\n",
                func_name, entry->dll_name, dll_name);
    }
    return entry->address;
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
            } else {
                /* Name import */
                IMAGE_IMPORT_BY_NAME *imp_name = (IMAGE_IMPORT_BY_NAME *)((char *)base + orig_thunks[i].AddressOfData);
                addr = resolve_import(dll_name, (const char *)imp_name->Name);
            }

            if (addr != NULL) {
                DEBUG("    Resolved %s -> %p",
                      orig_thunks[i].AddressOfData & 0x8000000000000000ULL ?
                      "<ordinal>" :
                      ((IMAGE_IMPORT_BY_NAME *)((char *)base + orig_thunks[i].AddressOfData))->Name,
                      addr);
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
    uint64_t import_dir_end = import_dir_va + opt->DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT].Size;

    int matched = 0;
    for (int t = 0; t < num_targets; t++) {
        uint64_t target = targets[t];
        uint64_t *target_ptr = (uint64_t *)((char *)base + target);
        uint64_t current_val = *target_ptr;

        if (strategy_resolved_overlap(current_val, flat, num_flat) ||
            strategy_ilt_value_match(target_ptr, current_val, target, flat, num_flat) ||
            strategy_ilt_offset_match(target_ptr, target, current_val,
                                      import_dir_va, import_dir_end, flat, num_flat) ||
            strategy_positional(target_ptr, target, t, flat, num_flat)) {
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
