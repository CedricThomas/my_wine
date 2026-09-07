/*
 * peb_ldr.h — PEB LDR data structures and management
 *
 * Windows-compatible PEB_LDR_DATA structure.
 * LIST_ENTRY, LDR_DATA_TABLE_ENTRY, LDR_UNICODE_STRING, and EXPORT_CACHE
 * are defined in module_list.h (embedded in loaded_module_t).
 */

#ifndef MY_WINE_PEB_LDR_H
#define MY_WINE_PEB_LDR_H

#include <stdint.h>
#include "module_list.h"  /* LIST_ENTRY, LDR_DATA_TABLE_ENTRY, loaded_module_t */

/* ── PEB_LDR_DATA ──────────────────────────────────────────────── */
typedef struct peb_ldr_data {
    uint8_t Reserved[8];
    LIST_ENTRY  ;
    LIST_ENTRY InMemoryOrderModuleList;
    LIST_ENTRY InInitializationOrderModuleList;
    uint8_t Reserved2[32];   /* Windows x64: 0x38–0x57, total 0x58 */
} PEB_LDR_DATA;

/* Public API */
PEB_LDR_DATA *init_peb_ldr(void);
int ldr_add_module(loaded_module_t *mod);
int ldr_remove_module(loaded_module_t *mod);
LDR_DATA_TABLE_ENTRY *ldr_find_by_addr(void *addr);

#endif /* MY_WINE_PEB_LDR_H */
