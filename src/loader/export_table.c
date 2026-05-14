/*
 * export_table.c — Parse export tables and lookup exports
 *
 * Reads IMAGE_EXPORT_DIRECTORY from a loaded PE image and caches
 * the address-of-functions, name, and ordinal tables into the
 * embedded EXPORT_CACHE within loaded_module_t.
 *
 * No malloc — the cache is embedded. No glibc — all memory ops
 * use dll_memcpy/dll_memset macros (no PLT calls).
 * Glibc-free: safe to call from WINE_STUB context after GS→TEB switch.
 */

#include <stdint.h>

#include "export_table.h"
#include "include/pe_priv.h"
#include "include/nt_constants.h"
#include "loader_utils.h"

int parse_export_table(loaded_module_t *mod)
{
    if (!mod || !mod->base || !mod->nt) {
        return -1;
    }

    IMAGE_DATA_DIRECTORY dir;
    if (!pe_get_data_dir(mod->nt, DIRECTORY_ENTRY_EXPORT, &dir) || dir.VirtualAddress == 0) {
        return -1;  /* No export directory */
    }

    IMAGE_EXPORT_DIRECTORY *exp =
        (IMAGE_EXPORT_DIRECTORY *)((uint8_t *)mod->base + dir.VirtualAddress);

    EXPORT_CACHE *cache = &mod->export_cache;

    /* Zero the entire cache — glibc-free */
    dll_memset(cache, 0, sizeof(EXPORT_CACHE));

    cache->base = mod->base;
    cache->export_dir_rva = dir.VirtualAddress;
    cache->export_dir_size = dir.Size;
    cache->address_of_functions = exp->AddressOfFunctions;
    cache->address_of_names = exp->AddressOfNames;
    cache->address_of_name_ordinals = exp->AddressOfNameOrdinals;
    cache->number_of_functions = exp->NumberOfFunctions;
    cache->number_of_names = exp->NumberOfNames;
    cache->base_ordinal = exp->Base;

    /* Copy AddressOfNames array (RVAs) */
    if (exp->NumberOfNames > 0 && exp->AddressOfNames != 0) {
        if (exp->NumberOfNames > MAX_EXPORT_NAMES) {
            exp->NumberOfNames = MAX_EXPORT_NAMES;
        }
        size_t nsize = exp->NumberOfNames * sizeof(uint32_t);
        uint32_t *src = (uint32_t *)((uint8_t *)mod->base + exp->AddressOfNames);
        dll_memcpy(cache->name_table, src, nsize);
    }

    /* Copy AddressOfNameOrdinals array (uint16_t ordinals) */
    if (exp->NumberOfNames > 0 && exp->AddressOfNameOrdinals != 0) {
        uint32_t names = exp->NumberOfNames;  /* already capped above */
        size_t osize = names * sizeof(uint16_t);
        uint16_t *src = (uint16_t *)((uint8_t *)mod->base + exp->AddressOfNameOrdinals);
        dll_memcpy(cache->ordinal_table, src, osize);
    }

    /* Copy AddressOfFunctions array (RVAs) */
    if (exp->NumberOfFunctions > 0 && exp->AddressOfFunctions != 0) {
        if (exp->NumberOfFunctions > MAX_EXPORT_FUNCTIONS) {
            exp->NumberOfFunctions = MAX_EXPORT_FUNCTIONS;
        }
        size_t fsize = exp->NumberOfFunctions * sizeof(uint32_t);
        uint32_t *src = (uint32_t *)((uint8_t *)mod->base + exp->AddressOfFunctions);
        dll_memcpy(cache->func_table, src, fsize);
    }

    return 0;
}

/*
 * check_forwarder — If func_rva falls inside the export directory,
 * it is a forwarder string ("dll!func"). Return true.
 * Forwarder resolution is deferred for now.
 * Glibc-free: no debug output to avoid fprintf@plt.
 */
static int check_forwarder(EXPORT_CACHE *cache, loaded_module_t *mod, uint32_t func_rva)
{
    (void)mod;
    uint32_t export_end = cache->export_dir_rva + cache->export_dir_size;

    if (func_rva >= cache->export_dir_rva && func_rva < export_end) {
        return 1;
    }
    return 0;
}

void *lookup_export(loaded_module_t *mod, const char *func_name)
{
    if (!mod) return NULL;
    EXPORT_CACHE *cache = &mod->export_cache;
    if (cache->number_of_names == 0) {
        return NULL;
    }

    uint32_t lo = 0;
    uint32_t hi = cache->number_of_names;

    while (lo < hi) {
        uint32_t mid = lo + (hi - lo) / 2;
        uint32_t name_rva = cache->name_table[mid];
        const char *name = (const char *)((uint8_t *)mod->base + name_rva);
        int cmp = dll_strcmp(func_name, name);

        if (cmp < 0) {
            hi = mid;
        } else if (cmp > 0) {
            lo = mid + 1;
        } else {
            /* Found — resolve through ordinal table then function table */
            uint16_t ordinal = cache->ordinal_table[mid];
            if (cache->number_of_functions == 0) {
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

void *lookup_export_by_ordinal(loaded_module_t *mod, uint16_t ordinal)
{
    if (!mod) return NULL;
    EXPORT_CACHE *cache = &mod->export_cache;
    if (cache->number_of_functions == 0) {
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

/* Reset the embedded export cache (clear all fields).
 * No free needed — the cache is embedded in the module.
 * Glibc-free: uses dll_memset instead of __builtin_memset. */
void reset_export_cache(loaded_module_t *mod)
{
    if (!mod) return;
    dll_memset(&mod->export_cache, 0, sizeof(EXPORT_CACHE));
}
