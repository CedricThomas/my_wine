/*
 * import_flat.c -- Flat import enumeration and thunk patch strategies
 *
 * This keeps pass-2 import patch mechanics separate from the static import
 * entry table definitions in import_table.c.
 */

#include <string.h>
#include <stdint.h>

#include "include/pe.h"
#include "include/pe_parser.h"
#include "include/common.h"
#include "src/pe_priv.h"
#include "include/debug.h"

#include "import_table.h"
#include "ordinal_table.h"

/**
 * Build a flat array of (ILT value, resolved address, func name) from
 * all import descriptors, in DLL order.
 * Returns the number of flat entries created.
 */
int build_flat_import_array(void *base, IMAGE_NT_HEADERS *nt,
                            struct import_flat flat[])
{
    IMAGE_DATA_DIRECTORY imp_dir;
    if (!pe_get_import_dir(nt, &imp_dir)) return 0;
    if (imp_dir.VirtualAddress == 0 ||
        !pe_rva_range_is_valid(imp_dir.VirtualAddress, imp_dir.Size,
                               pe_size_of_image(nt))) {
        return 0;
    }
    uint32_t import_rva = imp_dir.VirtualAddress;
    IMAGE_IMPORT_DESCRIPTOR *desc_start = pe_rva_to_ptr(base, nt, import_rva,
                                                        sizeof(IMAGE_IMPORT_DESCRIPTOR));
    if (desc_start == NULL) return 0;

    bool is32 = pe_is_pe32(nt);
    int num_flat = 0;
    IMAGE_IMPORT_DESCRIPTOR *desc = desc_start;
    uint32_t desc_offset = 0;
    while (desc_offset + sizeof(IMAGE_IMPORT_DESCRIPTOR) <= imp_dir.Size &&
           desc->Name != 0 && num_flat < MAX_FLAT_IMPORTS) {
        if (!pe_rva_range_is_valid(desc->Name, 1, pe_size_of_image(nt)))
            break;
        const char *dll_name = (const char *)base + desc->Name;

        if (is32) {
            uint32_t ilt_rva = desc->u1.OriginalFirstThunk != 0
                               ? desc->u1.OriginalFirstThunk
                               : desc->FirstThunk;
            IMAGE_THUNK_DATA32 *orig_thunks = pe_rva_to_ptr(base, nt, ilt_rva,
                                                            sizeof(IMAGE_THUNK_DATA32));
            IMAGE_THUNK_DATA32 *iath = pe_rva_to_ptr(base, nt, desc->FirstThunk,
                                                     sizeof(IMAGE_THUNK_DATA32));
            if (orig_thunks == NULL || iath == NULL)
                break;

            for (int i = 0; orig_thunks[i].AddressOfData != 0 && num_flat < MAX_FLAT_IMPORTS; i++) {
                size_t thunk_off = (size_t)i * sizeof(IMAGE_THUNK_DATA32);
                if (thunk_off / sizeof(IMAGE_THUNK_DATA32) != (size_t)i ||
                    thunk_off > SIZE_MAX - sizeof(IMAGE_THUNK_DATA32) ||
                    !pe_rva_range_is_valid(ilt_rva, thunk_off + sizeof(IMAGE_THUNK_DATA32),
                                           pe_size_of_image(nt)) ||
                    !pe_rva_range_is_valid(desc->FirstThunk, thunk_off + sizeof(IMAGE_THUNK_DATA32),
                                           pe_size_of_image(nt))) {
                    return num_flat;
                }
                flat[num_flat].ilt_value = orig_thunks[i].AddressOfData;
                flat[num_flat].resolved_addr = (uint64_t)(uint32_t)iath[i].AddressOfData;
                flat[num_flat].iat_addr = (uint64_t)(uintptr_t)&iath[i].AddressOfData;
                flat[num_flat].dll_name = dll_name;
                if (orig_thunks[i].AddressOfData & 0x80000000) {
                    uint16_t ordinal = (uint16_t)(orig_thunks[i].AddressOfData & 0xFFFF);
                    const char *fname = ordinal_lookup(dll_name, ordinal);
                    flat[num_flat].func_name = fname ? fname : "<ordinal>";
                } else {
                    IMAGE_IMPORT_BY_NAME *imp_name = pe_rva_to_ptr(base, nt,
                        orig_thunks[i].AddressOfData, sizeof(IMAGE_IMPORT_BY_NAME));
                    if (imp_name == NULL)
                        return num_flat;
                    flat[num_flat].func_name = (const char *)imp_name->Name;
                }
                num_flat++;
            }
        } else {
            uint32_t ilt_rva = desc->u1.OriginalFirstThunk != 0
                               ? desc->u1.OriginalFirstThunk
                               : desc->FirstThunk;
            IMAGE_THUNK_DATA64 *orig_thunks = pe_rva_to_ptr(base, nt, ilt_rva,
                                                            sizeof(IMAGE_THUNK_DATA64));
            IMAGE_THUNK_DATA64 *iath = pe_rva_to_ptr(base, nt, desc->FirstThunk,
                                                     sizeof(IMAGE_THUNK_DATA64));
            if (orig_thunks == NULL || iath == NULL)
                break;

            for (int i = 0; orig_thunks[i].AddressOfData != 0 && num_flat < MAX_FLAT_IMPORTS; i++) {
                size_t thunk_off = (size_t)i * sizeof(IMAGE_THUNK_DATA64);
                if (thunk_off / sizeof(IMAGE_THUNK_DATA64) != (size_t)i ||
                    thunk_off > SIZE_MAX - sizeof(IMAGE_THUNK_DATA64) ||
                    !pe_rva_range_is_valid(ilt_rva, thunk_off + sizeof(IMAGE_THUNK_DATA64),
                                           pe_size_of_image(nt)) ||
                    !pe_rva_range_is_valid(desc->FirstThunk, thunk_off + sizeof(IMAGE_THUNK_DATA64),
                                           pe_size_of_image(nt))) {
                    return num_flat;
                }
                flat[num_flat].ilt_value = orig_thunks[i].AddressOfData;
                flat[num_flat].resolved_addr = iath[i].AddressOfData;
                flat[num_flat].iat_addr = (uint64_t)(uintptr_t)&iath[i].AddressOfData;
                flat[num_flat].dll_name = dll_name;
                if (orig_thunks[i].AddressOfData & 0x8000000000000000ULL) {
                    uint16_t ordinal = (uint16_t)(orig_thunks[i].AddressOfData & 0xFFFF);
                    const char *fname = ordinal_lookup(dll_name, ordinal);
                    flat[num_flat].func_name = fname ? fname : "<ordinal>";
                } else {
                    if (orig_thunks[i].AddressOfData > UINT32_MAX)
                        return num_flat;
                    IMAGE_IMPORT_BY_NAME *imp_name = pe_rva_to_ptr(base, nt,
                        (uint32_t)orig_thunks[i].AddressOfData, sizeof(IMAGE_IMPORT_BY_NAME));
                    if (imp_name == NULL)
                        return num_flat;
                    flat[num_flat].func_name = (const char *)imp_name->Name;
                }
                num_flat++;
            }
        }
        desc_offset += sizeof(IMAGE_IMPORT_DESCRIPTOR);
        desc = pe_rva_to_ptr(base, nt, import_rva + desc_offset,
                             sizeof(IMAGE_IMPORT_DESCRIPTOR));
        if (desc == NULL)
            break;
    }

    return num_flat;
}

/**
 * Strategy 1: target overlaps with a resolved import address.
 * Only check the flat entry whose iat_addr matches the target IAT entry.
 * Does NOT write -- the value is already correct (from pass 1 IAT).
 */
bool strategy_resolved_overlap(uint64_t current_val, void *target_ptr,
                               struct import_flat *flat, int num_flat)
{
    uint64_t target_addr = (uint64_t)(uintptr_t)target_ptr;
    for (int f = 0; f < num_flat; f++) {
        if (flat[f].iat_addr == target_addr && flat[f].resolved_addr == current_val) {
            return true;
        }
    }
    return false;
}

/**
 * Strategy 2: ILT entry value equals a resolved address.
 * Only match the flat entry whose iat_addr matches the target IAT entry.
 * If current_val matches an ilt_value whose resolved_addr is set,
 * write the resolved_addr to the target location.
 */
bool strategy_ilt_value_match(void *target_ptr, uint64_t current_val,
                              uint64_t target, size_t thunk_size,
                              struct import_flat *flat, int num_flat)
{
    if (current_val == 0)
        return false;
    uint64_t target_addr = (uint64_t)(uintptr_t)target_ptr;
    for (int f = 0; f < num_flat; f++) {
        if (flat[f].iat_addr == target_addr &&
            flat[f].ilt_value == current_val && flat[f].resolved_addr != 0) {
            memcpy(target_ptr, &flat[f].resolved_addr, thunk_size);
            DEBUG("    Thunk patch (ilt match): %s!%s at 0x%lx <- 0x%lx",
                  flat[f].dll_name, flat[f].func_name,
                  (unsigned long)target, (unsigned long)flat[f].resolved_addr);
            return true;
        }
    }
    return false;
}

/**
 * Strategy 3: Direct IAT address lookup via per-descriptor iat_addr.
 * Find the flat entry whose iat_addr matches the target IAT entry address.
 * This provides exact per-descriptor IAT range awareness -- each entry
 * knows which DLL's IAT it belongs to by its iat_addr.
 */
bool strategy_ilt_offset_match(void *target_ptr, uint64_t target,
                               uint64_t current_val, size_t thunk_size,
                               struct import_flat *flat, int num_flat)
{
    if (current_val == 0)
        return false;

    uint64_t target_addr = (uint64_t)(uintptr_t)target_ptr;
    for (int f = 0; f < num_flat; f++) {
        if (flat[f].iat_addr == target_addr && flat[f].resolved_addr != 0) {
            memcpy(target_ptr, &flat[f].resolved_addr, thunk_size);
            DEBUG("    Thunk patch (ilt-offset match): %s!%s at 0x%lx <- 0x%lx",
                  flat[f].dll_name, flat[f].func_name,
                  (unsigned long)target, (unsigned long)flat[f].resolved_addr);
            return true;
        }
    }

    return false;
}
