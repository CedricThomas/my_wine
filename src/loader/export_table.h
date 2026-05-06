/*
 * export_table.h — Export table cache and lookup
 */

#ifndef MY_WINE_EXPORT_TABLE_H
#define MY_WINE_EXPORT_TABLE_H

#include <stdint.h>
#include "include/pe.h"
#include "module_list.h"

/* Cached export data for a module */
typedef struct export_cache {
    void *base;                        /* Module base address */
    uint32_t export_dir_rva;          /* RVA of IMAGE_EXPORT_DIRECTORY */
    uint32_t export_dir_size;         /* Size of export directory */
    uint32_t address_of_functions;    /* RVA of function address table */
    uint32_t address_of_names;        /* RVA of name pointer table */
    uint32_t address_of_name_ordinals;/* RVA of ordinal table */
    uint32_t number_of_functions;
    uint32_t number_of_names;
    uint32_t base_ordinal;
    /* Copied data for lookup */
    uint32_t *name_table;             /* number_of_names entries (RVAs) */
    uint16_t *ordinal_table;          /* number_of_names entries */
    uint32_t *func_table;             /* number_of_functions entries (RVAs) */
} EXPORT_CACHE;

/* Parse the export table of a module and populate its export_cache.
 * Allocates the cache via malloc; caller stores in mod->export_cache.
 * Returns the allocated cache on success, NULL if no export directory. */
EXPORT_CACHE *parse_export_table(void *base, IMAGE_NT_HEADERS64 *nt);

/* Lookup an export by name. Returns the absolute address of the function,
 * or NULL if not found. */
void *lookup_export(loaded_module_t *mod, const char *func_name);

/* Lookup an export by ordinal. Returns the absolute address of the function,
 * or NULL if not found. Ordinal is the full ordinal (includes base). */
void *lookup_export_by_ordinal(loaded_module_t *mod, uint16_t ordinal);

/* Free the export cache (including its malloc'd arrays). */
void free_export_cache(EXPORT_CACHE *cache);

#endif /* MY_WINE_EXPORT_TABLE_H */
