/*
 * import_table.h — Import table definitions and strategies
 *
 * Declares the import_entry_t/import_flat types, the import_table[]
 * global, and the resolution strategy functions.
 */

#ifndef MY_WINE_IMPORT_TABLE_H
#define MY_WINE_IMPORT_TABLE_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#include "include/pe.h"

/* ── Import resolver types ─────────────────────────────────── */

typedef struct {
    const char *dll_name;
    const char *name;
    void *address;
} import_entry_t;

/* Flat import entry used in pass 2 thunk patching */
struct import_flat {
    uint64_t   ilt_value;      /* OriginalFirstThunk[i].AddressOfData */
    uint64_t   resolved_addr;  /* FirstThunk[i].AddressOfData (from pass 1) */
    uint64_t   iat_addr;       /* Actual address of the IAT entry (FirstThunk[i]) */
    const char *dll_name;
    const char *func_name;
};

/* Name→address table for NT, kernel32 and msvcrt functions */
extern import_entry_t import_table[];
extern size_t import_table_count;

/* Import table management */
void set_import(const char *name, void *address);
void init_import_table(void);
int import_cmp_by_name(const void *key, const void *elem);
int build_flat_import_array(void *base, IMAGE_NT_HEADERS *nt,
                            struct import_flat flat[]);

/* Pass 2 resolution strategies */
bool strategy_resolved_overlap(uint64_t current_val, void *target_ptr,
                               struct import_flat *flat, int num_flat);
bool strategy_ilt_value_match(void *target_ptr, uint64_t current_val,
                              uint64_t target, size_t thunk_size,
                              struct import_flat *flat, int num_flat);
bool strategy_ilt_offset_match(void *target_ptr, uint64_t target,
                               uint64_t current_val, size_t thunk_size,
                               struct import_flat *flat, int num_flat);
#endif /* MY_WINE_IMPORT_TABLE_H */
