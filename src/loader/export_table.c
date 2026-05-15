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
#include "src/pe_priv.h"
#include "include/nt_constants.h"
#include "loader_utils.h"

static int image_cstr_valid(const loaded_module_t *mod, uint32_t rva)
{
    size_t image_size;

    if (!mod || !mod->nt)
        return 0;
    image_size = pe_size_of_image(mod->nt);
    if (!pe_rva_range_is_valid(rva, 1, image_size))
        return 0;
    const char *s = (const char *)((const uint8_t *)mod->base + rva);
    for (size_t off = rva; off < image_size; off++) {
        if (*s++ == '\0')
            return 1;
    }
    return 0;
}

int parse_export_table(loaded_module_t *mod)
{
    if (!mod || !mod->base || !mod->nt) {
        return -1;
    }

    IMAGE_DATA_DIRECTORY dir;
    if (!pe_get_data_dir(mod->nt, DIRECTORY_ENTRY_EXPORT, &dir) || dir.VirtualAddress == 0) {
        return -1;  /* No export directory */
    }
    if (dir.Size < sizeof(IMAGE_EXPORT_DIRECTORY)) {
        return -1;
    }

    IMAGE_EXPORT_DIRECTORY *exp =
        pe_rva_to_ptr(mod->base, mod->nt, dir.VirtualAddress,
                      sizeof(IMAGE_EXPORT_DIRECTORY));
    if (exp == NULL) {
        return -1;
    }

    EXPORT_CACHE *cache = &mod->export_cache;
    uint32_t number_of_names = exp->NumberOfNames;
    uint32_t number_of_functions = exp->NumberOfFunctions;
    if (number_of_names > MAX_EXPORT_NAMES) {
        number_of_names = MAX_EXPORT_NAMES;
    }
    if (number_of_functions > MAX_EXPORT_FUNCTIONS) {
        number_of_functions = MAX_EXPORT_FUNCTIONS;
    }

    /* Zero the entire cache — glibc-free */
    dll_memset(cache, 0, sizeof(EXPORT_CACHE));

    cache->base = mod->base;
    cache->export_dir_rva = dir.VirtualAddress;
    cache->export_dir_size = dir.Size;
    cache->address_of_functions = exp->AddressOfFunctions;
    cache->address_of_names = exp->AddressOfNames;
    cache->address_of_name_ordinals = exp->AddressOfNameOrdinals;
    cache->number_of_functions = number_of_functions;
    cache->number_of_names = number_of_names;
    cache->base_ordinal = exp->Base;

    if ((number_of_names > 0 &&
         (exp->AddressOfNames == 0 || exp->AddressOfNameOrdinals == 0)) ||
        (number_of_functions > 0 && exp->AddressOfFunctions == 0)) {
        dll_memset(cache, 0, sizeof(EXPORT_CACHE));
        return -1;
    }

    /* Copy AddressOfNames array (RVAs) */
    if (number_of_names > 0 && exp->AddressOfNames != 0) {
        size_t nsize = number_of_names * sizeof(uint32_t);
        uint32_t *src = pe_rva_to_ptr(mod->base, mod->nt,
                                      exp->AddressOfNames, nsize);
        if (src == NULL) {
            dll_memset(cache, 0, sizeof(EXPORT_CACHE));
            return -1;
        }
        dll_memcpy(cache->name_table, src, nsize);
    }

    /* Copy AddressOfNameOrdinals array (uint16_t ordinals) */
    if (number_of_names > 0 && exp->AddressOfNameOrdinals != 0) {
        uint32_t names = number_of_names;
        size_t osize = names * sizeof(uint16_t);
        uint16_t *src = pe_rva_to_ptr(mod->base, mod->nt,
                                      exp->AddressOfNameOrdinals, osize);
        if (src == NULL) {
            dll_memset(cache, 0, sizeof(EXPORT_CACHE));
            return -1;
        }
        dll_memcpy(cache->ordinal_table, src, osize);
    }

    /* Copy AddressOfFunctions array (RVAs) */
    if (number_of_functions > 0 && exp->AddressOfFunctions != 0) {
        size_t fsize = number_of_functions * sizeof(uint32_t);
        uint32_t *src = pe_rva_to_ptr(mod->base, mod->nt,
                                      exp->AddressOfFunctions, fsize);
        if (src == NULL) {
            dll_memset(cache, 0, sizeof(EXPORT_CACHE));
            return -1;
        }
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
    if (cache->export_dir_size > UINT32_MAX - cache->export_dir_rva)
        return 0;
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
        if (!image_cstr_valid(mod, name_rva)) {
            return NULL;
        }
        const char *name = (const char *)pe_rva_to_ptr(mod->base, mod->nt,
                                                       name_rva, 1);
        int cmp = dll_strcmp(func_name, name);

        if (cmp < 0) {
            hi = mid;
        } else if (cmp > 0) {
            lo = mid + 1;
        } else {
            /* Found — resolve through ordinal table then function table */
            uint16_t ordinal = cache->ordinal_table[mid];
            if (cache->number_of_functions == 0 ||
                ordinal >= cache->number_of_functions) {
                return NULL;
            }
            uint32_t func_rva = cache->func_table[ordinal];
            if (check_forwarder(cache, mod, func_rva)) {
                return NULL;
            }
            return pe_rva_to_ptr(mod->base, mod->nt, func_rva, 1);
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
    return pe_rva_to_ptr(mod->base, mod->nt, func_rva, 1);
}

/* Reset the embedded export cache (clear all fields).
 * No free needed — the cache is embedded in the module.
 * Glibc-free: uses dll_memset instead of __builtin_memset. */
void reset_export_cache(loaded_module_t *mod)
{
    if (!mod) return;
    dll_memset(&mod->export_cache, 0, sizeof(EXPORT_CACHE));
}
