/*
 * export_table.c — Parse export tables and lookup exports
 *
 * Reads IMAGE_EXPORT_DIRECTORY from a loaded PE image and caches
 * the address-of-functions, name, and ordinal tables into heap
 * buffers so they survive mprotect changes to the image.
 */

#define _GNU_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>

#include "export_table.h"
#include "include/nt_constants.h"
#include "include/debug.h"
#include "module_list.h"

/*
 * parse_export_table — Read the export directory and copy the three
 * lookup tables (names, ordinals, function addresses) into heap-
 * allocated buffers inside an EXPORT_CACHE.
 *
 * Returns NULL when the module has no export directory
 * (DataDirectory[0].VirtualAddress == 0).
 */
EXPORT_CACHE *parse_export_table(void *base, IMAGE_NT_HEADERS64 *nt)
{
    const IMAGE_DATA_DIRECTORY *dir =
        &nt->OptionalHeader.DataDirectory[DIRECTORY_ENTRY_EXPORT];

    if (dir->VirtualAddress == 0) {
        return NULL;
    }

    IMAGE_EXPORT_DIRECTORY *exp =
        (IMAGE_EXPORT_DIRECTORY *)((uint8_t *)base + dir->VirtualAddress);

    /* Allocate the cache struct */
    EXPORT_CACHE *cache = malloc(sizeof(EXPORT_CACHE));
    if (!cache) {
        return NULL;
    }

    cache->base = base;
    cache->export_dir_rva = dir->VirtualAddress;
    cache->export_dir_size = dir->Size;
    cache->address_of_functions = exp->AddressOfFunctions;
    cache->address_of_names = exp->AddressOfNames;
    cache->address_of_name_ordinals = exp->AddressOfNameOrdinals;
    cache->number_of_functions = exp->NumberOfFunctions;
    cache->number_of_names = exp->NumberOfNames;
    cache->base_ordinal = exp->Base;

    /* Copy AddressOfNames array (RVAs) */
    if (exp->NumberOfNames > 0 && exp->AddressOfNames != 0) {
        size_t nsize = exp->NumberOfNames * sizeof(uint32_t);
        uint32_t *src = (uint32_t *)((uint8_t *)base + exp->AddressOfNames);
        cache->name_table = malloc(nsize);
        if (!cache->name_table) {
            free(cache);
            return NULL;
        }
        memcpy(cache->name_table, src, nsize);
    } else {
        cache->name_table = NULL;
    }

    /* Copy AddressOfNameOrdinals array (uint16_t ordinals) */
    if (exp->NumberOfNames > 0 && exp->AddressOfNameOrdinals != 0) {
        size_t osize = exp->NumberOfNames * sizeof(uint16_t);
        uint16_t *src = (uint16_t *)((uint8_t *)base + exp->AddressOfNameOrdinals);
        cache->ordinal_table = malloc(osize);
        if (!cache->ordinal_table) {
            free(cache->name_table);
            free(cache);
            return NULL;
        }
        memcpy(cache->ordinal_table, src, osize);
    } else {
        cache->ordinal_table = NULL;
    }

    /* Copy AddressOfFunctions array (RVAs) */
    if (exp->NumberOfFunctions > 0 && exp->AddressOfFunctions != 0) {
        size_t fsize = exp->NumberOfFunctions * sizeof(uint32_t);
        uint32_t *src = (uint32_t *)((uint8_t *)base + exp->AddressOfFunctions);
        cache->func_table = malloc(fsize);
        if (!cache->func_table) {
            free(cache->ordinal_table);
            free(cache->name_table);
            free(cache);
            return NULL;
        }
        memcpy(cache->func_table, src, fsize);
    } else {
        cache->func_table = NULL;
    }

    return cache;
}

/*
 * check_forwarder — If func_rva falls inside the export directory,
 * it is a forwarder string ("dll!func"). Log a warning and return true.
 * Forwarder resolution is deferred for now.
 */
static int check_forwarder(EXPORT_CACHE *cache, loaded_module_t *mod, uint32_t func_rva)
{
    uint32_t export_end = cache->export_dir_rva + cache->export_dir_size;

    if (func_rva >= cache->export_dir_rva && func_rva < export_end) {
        const char *forwarder_str = (const char *)((uint8_t *)mod->base + func_rva);
        fprintf(stderr, "my_wine: forwarder detected: %s\n", forwarder_str);
        return 1;
    }
    return 0;
}

/*
 * lookup_export — Find an export by name via binary search on the
 * AddressOfNames table.
 *
 * Returns the absolute runtime address, or NULL if not found.
 */
void *lookup_export(loaded_module_t *mod, const char *func_name)
{
    if (!mod) return NULL;
    EXPORT_CACHE *cache = mod->export_cache;
    if (!cache || !cache->name_table || cache->number_of_names == 0) {
        return NULL;
    }

    uint32_t lo = 0;
    uint32_t hi = cache->number_of_names;

    while (lo < hi) {
        uint32_t mid = lo + (hi - lo) / 2;
        uint32_t name_rva = cache->name_table[mid];
        const char *name = (const char *)((uint8_t *)mod->base + name_rva);
        int cmp = strcmp(func_name, name);

        if (cmp < 0) {
            hi = mid;
        } else if (cmp > 0) {
            lo = mid + 1;
        } else {
            /* Found — resolve through ordinal table then function table */
            uint16_t ordinal = cache->ordinal_table[mid];
            if (!cache->func_table) {
                return NULL;
            }
            uint32_t func_rva = cache->func_table[ordinal];
            if (check_forwarder(cache, mod, func_rva)) {
                return NULL;
            }
            return (void *)((uint8_t *)mod->base + func_rva);
        }
    }

    return NULL;
}

/*
 * lookup_export_by_ordinal — Direct lookup by ordinal number.
 *
 * The ordinal is the full ordinal (includes base_ordinal).
 * Returns the absolute runtime address, or NULL if not found.
 */
void *lookup_export_by_ordinal(loaded_module_t *mod, uint16_t ordinal)
{
    if (!mod) return NULL;
    EXPORT_CACHE *cache = mod->export_cache;
    if (!cache || !cache->func_table) {
        return NULL;
    }

    uint32_t idx = ordinal - cache->base_ordinal;
    if (idx >= cache->number_of_functions) {
        return NULL;
    }

    uint32_t func_rva = cache->func_table[idx];
    if (check_forwarder(cache, mod, func_rva)) {
        return NULL;
    }
    return (void *)((uint8_t *)mod->base + func_rva);
}

/*
 * free_export_cache — Release all heap-allocated buffers.
 */
void free_export_cache(EXPORT_CACHE *cache)
{
    if (!cache) {
        return;
    }
    free(cache->name_table);
    free(cache->ordinal_table);
    free(cache->func_table);
    free(cache);
}
