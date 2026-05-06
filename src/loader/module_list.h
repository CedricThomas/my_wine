/*
 * module_list.h — Module registry for tracking loaded PE images
 */

#ifndef MY_WINE_MODULE_LIST_H
#define MY_WINE_MODULE_LIST_H

#include <stdint.h>
#include <stddef.h>

#include "include/pe.h"

/* Forward declarations (defined in peb_ldr.h) */
typedef struct ldr_data_table_entry LDR_DATA_TABLE_ENTRY;
typedef struct export_cache EXPORT_CACHE;

/* One entry per loaded module (exe or dll) */
typedef struct {
    void *base;                  /* Mapped base address */
    char name[260];             /* Module name (e.g., "kernel32.dll") */
    IMAGE_NT_HEADERS64 *nt;     /* Pointer to NT headers in image memory */
    EXPORT_CACHE *export_cache; /* Cached export table (from export_table.c) */
    LDR_DATA_TABLE_ENTRY *ldr_entry; /* PEB LDR entry */
} loaded_module_t;

#define MAX_MODULES 16

/* Public API */
void init_module_list(void);
loaded_module_t *add_module(void *base, const char *name, IMAGE_NT_HEADERS64 *nt);
loaded_module_t *find_module_by_name(const char *name);
loaded_module_t *find_module_by_addr(void *addr);
void remove_module(loaded_module_t *mod);

/* Direct access (for PEB LDR integration) */
extern loaded_module_t module_list[MAX_MODULES];
extern int module_count;

#endif /* MY_WINE_MODULE_LIST_H */
