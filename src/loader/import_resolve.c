/*
 * import_resolve.c — IAT resolution (pass 1 + pass 2 + thunk strategies)
 *
 * Resolves imports by patching IAT entries in the PE image.
 */

#define _GNU_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <search.h>
#include <strings.h>
#include <stdbool.h>

#include "include/pe.h"
#include "include/pe_parser.h"
#include "include/common.h"
#include "loader_priv.h"

/* Flat import entry used in pass 2 thunk patching */
struct import_flat {
    uint64_t   ilt_value;      /* OriginalFirstThunk[i].AddressOfData */
    uint64_t   resolved_addr;  /* FirstThunk[i].AddressOfData (from pass 1) */
    const char *dll_name;
    const char *func_name;
};

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
    size_t count = sizeof(import_table) / sizeof(import_entry_t) - 1;
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
        printf("No imports to resolve\n");
        return 0;
    }

    uint64_t import_rva = opt->DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT].VirtualAddress;
    IMAGE_IMPORT_DESCRIPTOR *desc = (IMAGE_IMPORT_DESCRIPTOR *)((char *)base + import_rva);

    printf("Resolving imports:\n");
    fflush(stdout);

    while (desc->Name != 0) {
        const char *dll_name = (const char *)((char *)base + desc->Name);

        printf("  DLL: %s\n", dll_name);

        IMAGE_THUNK_DATA64 *orig_thunks = (IMAGE_THUNK_DATA64 *)((char *)base + desc->u1.OriginalFirstThunk);
        IMAGE_THUNK_DATA64 *iath = (IMAGE_THUNK_DATA64 *)((char *)base + desc->FirstThunk);

        for (int i = 0; orig_thunks[i].AddressOfData != 0; i++) {
            void *addr = NULL;

            if (orig_thunks[i].AddressOfData & 0x8000000000000000ULL) {
                /* Ordinal import (high bit set) */
                uint64_t ordinal = orig_thunks[i].AddressOfData & 0xFFFF;
                fprintf(stderr, "  WARNING: ordinal import %lu not supported\n", ordinal);
                continue;
            } else {
                /* Name import */
                IMAGE_IMPORT_BY_NAME *imp_name = (IMAGE_IMPORT_BY_NAME *)((char *)base + orig_thunks[i].AddressOfData);
                addr = resolve_import(dll_name, (const char *)imp_name->Name);
            }

            if (addr != NULL) {
                printf("    Resolved %s -> %p\n",
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
 * Build a flat array of (ILT value, resolved address, func name) from
 * all import descriptors, in DLL order.
 * Returns the number of flat entries created.
 */
static int build_flat_import_array(void *base, IMAGE_NT_HEADERS64 *nt,
                                   struct import_flat flat[MAX_FLAT_IMPORTS])
{
    IMAGE_OPTIONAL_HEADER64 *opt = &nt->OptionalHeader;
    uint64_t import_rva = opt->DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT].VirtualAddress;
    IMAGE_IMPORT_DESCRIPTOR *desc_start = (IMAGE_IMPORT_DESCRIPTOR *)((char *)base + import_rva);

    int num_flat = 0;
    IMAGE_IMPORT_DESCRIPTOR *desc = desc_start;
    while (desc->Name != 0 && num_flat < MAX_FLAT_IMPORTS) {
        const char *dll_name = (const char *)((char *)base + desc->Name);
        IMAGE_THUNK_DATA64 *orig_thunks = (IMAGE_THUNK_DATA64 *)((char *)base + desc->u1.OriginalFirstThunk);
        IMAGE_THUNK_DATA64 *iath = (IMAGE_THUNK_DATA64 *)((char *)base + desc->FirstThunk);

        for (int i = 0; orig_thunks[i].AddressOfData != 0 && num_flat < MAX_FLAT_IMPORTS; i++) {
            flat[num_flat].ilt_value = orig_thunks[i].AddressOfData;
            flat[num_flat].resolved_addr = iath[i].AddressOfData;
            flat[num_flat].dll_name = dll_name;
            if (orig_thunks[i].AddressOfData & 0x8000000000000000ULL) {
                flat[num_flat].func_name = "<ordinal>";
            } else {
                IMAGE_IMPORT_BY_NAME *imp_name = (IMAGE_IMPORT_BY_NAME *)((char *)base + orig_thunks[i].AddressOfData);
                flat[num_flat].func_name = (const char *)imp_name->Name;
            }
            num_flat++;
        }
        desc++;
    }

    return num_flat;
}

/**
 * Strategy 1: target overlaps with a resolved import address.
 * Check if current value matches any resolved_addr in the flat array.
 * Does NOT write -- the value is already correct (from pass 1 IAT).
 */
static bool strategy_resolved_overlap(uint64_t current_val,
                                     struct import_flat *flat, int num_flat)
{
    for (int f = 0; f < num_flat; f++) {
        if (flat[f].resolved_addr == current_val) {
            return true;
        }
    }
    return false;
}

/**
 * Strategy 2: ILT entry value equals a resolved address.
 * If current_val matches an ilt_value whose resolved_addr is set,
 * write the resolved_addr to the target location.
 */
static bool strategy_ilt_value_match(uint64_t *target_ptr, uint64_t current_val,
                                    uint64_t target,
                                    struct import_flat *flat, int num_flat)
{
    if (current_val == 0)
        return false;
    for (int f = 0; f < num_flat; f++) {
        if (flat[f].ilt_value == current_val && flat[f].resolved_addr != 0) {
            *target_ptr = flat[f].resolved_addr;
            printf("    Thunk patch (ilt match): %s!%s at 0x%lx <- 0x%lx\n",
                   flat[f].dll_name, flat[f].func_name,
                   (unsigned long)target, (unsigned long)flat[f].resolved_addr);
            return true;
        }
    }
    return false;
}

/**
 * Strategy 3: ILT offset+slot matches thunk position.
 * If target falls within the import directory, compute slot index
 * and use the corresponding flat entry.
 */
static bool strategy_ilt_offset_match(uint64_t *target_ptr, uint64_t target,
                                     uint64_t current_val,
                                     uint64_t import_dir_va, uint64_t import_dir_end,
                                     struct import_flat *flat, int num_flat)
{
    if (current_val == 0 || target < import_dir_va || target >= import_dir_end)
        return false;
    int slot_idx = (int)((target - import_dir_va) / 8);
    if (slot_idx < 0 || slot_idx >= num_flat || flat[slot_idx].resolved_addr == 0)
        return false;
    *target_ptr = flat[slot_idx].resolved_addr;
    printf("    Thunk patch (ilt-offset match): %s!%s at 0x%lx <- 0x%lx\n",
           flat[slot_idx].dll_name, flat[slot_idx].func_name,
           (unsigned long)target, (unsigned long)flat[slot_idx].resolved_addr);
    return true;
}

/**
 * Strategy 4: fallback by position in the flat array.
 */
static bool strategy_positional(uint64_t *target_ptr, uint64_t target,
                               int thunk_idx,
                               struct import_flat *flat, int num_flat)
{
    if (thunk_idx >= num_flat || flat[thunk_idx].resolved_addr == 0)
        return false;
    *target_ptr = flat[thunk_idx].resolved_addr;
    printf("    Thunk patch (pos match): %s!%s at 0x%lx <- 0x%lx\n",
           flat[thunk_idx].dll_name, flat[thunk_idx].func_name,
           (unsigned long)target, (unsigned long)flat[thunk_idx].resolved_addr);
    return true;
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

    printf("  Found %d thunk targets in .text (range 0x%lx-0x%lx)\n",
           num_targets,
           (unsigned long)thunk_targets[0],
           (unsigned long)thunk_targets[num_targets - 1] + 7);

    struct import_flat flat[MAX_FLAT_IMPORTS];
    int num_flat = build_flat_import_array(base, nt, flat);

    printf("  Flat import array: %d entries\n", num_flat);

    int matched = patch_thunk_targets(base, nt, thunk_targets, num_targets, flat, num_flat);

    printf("  Thunk IAT patched: %d/%d targets resolved\n", matched, num_targets);

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
