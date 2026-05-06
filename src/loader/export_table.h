/*
 * export_table.h — Export table cache and lookup
 *
 * The EXPORT_CACHE struct is embedded in loaded_module_t (defined in
 * module_list.h). parse_export_table populates the embedded cache.
 */

#ifndef MY_WINE_EXPORT_TABLE_H
#define MY_WINE_EXPORT_TABLE_H

#include <stdint.h>
#include "module_list.h"

/* Populate the embedded export cache from the module's export directory.
 * Returns 0 on success, -1 if the module has no export directory.
 * The cache is written into mod->export_cache (no malloc). */
int parse_export_table(loaded_module_t *mod);

/* Lookup an export by name. Returns the absolute address of the function,
 * or NULL if not found. */
void *lookup_export(loaded_module_t *mod, const char *func_name);

/* Lookup an export by ordinal. Returns the absolute address of the function,
 * or NULL if not found. Ordinal is the full ordinal (includes base). */
void *lookup_export_by_ordinal(loaded_module_t *mod, uint16_t ordinal);

/* Reset the embedded export cache (clear all fields).
 * No free needed — the cache is embedded in the module. */
void reset_export_cache(loaded_module_t *mod);

#endif /* MY_WINE_EXPORT_TABLE_H */
