/*
 * module_list.h — Module registry for tracking loaded PE images
 */

#ifndef MY_WINE_MODULE_LIST_H
#define MY_WINE_MODULE_LIST_H

#include <stdint.h>
#include <stddef.h>

#include "include/pe.h"

/* ── Max sizes for embedded tables ─────────────────────────────── */
#define MAX_EXPORT_NAMES       2048
#define MAX_EXPORT_FUNCTIONS   8192
#define MAX_LDR_NAME_WCHAR     260

/* ── Windows LIST_ENTRY (doubly-linked list node) ────────────────── */
typedef struct LIST_ENTRY {
    struct LIST_ENTRY *Flink;
    struct LIST_ENTRY *Blink;
} LIST_ENTRY;

#define LIST_ENTRY_INIT(name) { &(name), &(name) }

/* ── UNICODE_STRING (PEB/LDR variant) ────────────────────────────── */
/*
 * 16-byte structure with embedded buffer for LDR entries.
 * Buffer is a fixed-size uint16_t array (UTF-16) to avoid malloc.
 */
typedef struct {
    uint16_t Length;
    uint16_t MaximumLength;
    uint16_t Buffer[MAX_LDR_NAME_WCHAR + 1];
} LDR_UNICODE_STRING;

/* ── LDR_DATA_TABLE_ENTRY (embedded in loaded_module_t) ─────────── */
typedef struct ldr_data_table_entry {
    LIST_ENTRY DoubleList[3];  /* [0]=InLoadOrder, [1]=InMemoryOrder, [2]=InInitializationOrder */
    void *DllBase;
    void *EntryPoint;
    uint32_t SizeOfImage;
    LDR_UNICODE_STRING FullDllName;
    LDR_UNICODE_STRING BaseDllName;
    uint32_t Flags;
    int16_t LoadCount;
    int16_t TlsIndex;
    LIST_ENTRY HashTableEntry;
    uint32_t TimeDateStamp;
    uint8_t  Padding[0x34];   /* 0x84–0xB7: Windows fields */
} LDR_DATA_TABLE_ENTRY;

/* ── Export cache (embedded in loaded_module_t) ─────────────────── */
typedef struct export_cache {
    void *base;
    uint32_t export_dir_rva;
    uint32_t export_dir_size;
    uint32_t address_of_functions;
    uint32_t address_of_names;
    uint32_t address_of_name_ordinals;
    uint32_t number_of_functions;
    uint32_t number_of_names;
    uint32_t base_ordinal;
    /* Embedded tables (no malloc needed) */
    uint32_t name_table[MAX_EXPORT_NAMES];         /* name RVAs */
    uint16_t ordinal_table[MAX_EXPORT_NAMES];      /* ordinals */
    uint32_t func_table[MAX_EXPORT_FUNCTIONS];     /* function RVAs */
} EXPORT_CACHE;

/* ── One entry per loaded module ─────────────────────────────────── */
typedef struct {
    void *base;                  /* Mapped base address */
    char name[260];             /* Module name (e.g., "kernel32.dll") */
    IMAGE_NT_HEADERS *nt;     /* Pointer to NT headers in image memory */
    EXPORT_CACHE export_cache;  /* Embedded export cache (no malloc) */
    LDR_DATA_TABLE_ENTRY ldr_entry; /* Embedded PEB LDR entry (no malloc) */
    int load_count;              /* Reference count */
    uint8_t ldr_linked;          /* Whether ldr_entry is linked in lists */
} loaded_module_t;

#define MAX_MODULES 16

/* Public API */
void init_module_list(void);
loaded_module_t *add_module(void *base, const char *name, IMAGE_NT_HEADERS *nt);
loaded_module_t *find_module_by_name(const char *name);
loaded_module_t *find_module_by_name_safe(const char *name);
loaded_module_t *find_module_by_addr(void *addr);
void remove_module(loaded_module_t *mod);

/* Direct access (for PEB LDR integration) */
extern loaded_module_t module_list[MAX_MODULES];
extern int module_count;

#endif /* MY_WINE_MODULE_LIST_H */
