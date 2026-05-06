/*
 * peb_ldr.h — PEB LDR data structures and management
 *
 * Windows-compatible PEB_LDR_DATA and LDR_DATA_TABLE_ENTRY structures,
 * with helpers to maintain the InLoadOrder/InMemoryOrder/InInitializationOrder
 * doubly-linked lists.
 */

#ifndef MY_WINE_PEB_LDR_H
#define MY_WINE_PEB_LDR_H

#include <stdint.h>
#include <wchar.h>
#include "include/pe.h"
#include "module_list.h"

/* ── Windows LIST_ENTRY (doubly-linked list node) ─────────────── */
typedef struct LIST_ENTRY {
    struct LIST_ENTRY *Flink;
    struct LIST_ENTRY *Blink;
} LIST_ENTRY;

#define LIST_ENTRY_INIT(name) { &(name), &(name) }

/* ── UNICODE_STRING ────────────────────────────────────────────── */
typedef struct {
    uint16_t Length;
    uint16_t MaximumLength;
    wchar_t *Buffer;
} UNICODE_STRING;

/* ── PEB_LDR_DATA ──────────────────────────────────────────────── */
typedef struct {
    uint8_t Reserved[8];
    LIST_ENTRY InLoadOrderModuleList;
    LIST_ENTRY InMemoryOrderModuleList;
    LIST_ENTRY InInitializationOrderModuleList;
    uint8_t Reserved2[24];
} PEB_LDR_DATA;

/* ── LDR_DATA_TABLE_ENTRY ───────────────────────────────────────── */
typedef struct ldr_data_table_entry {
    LIST_ENTRY DoubleList[3];  /* [0]=InLoadOrder, [1]=InMemoryOrder, [2]=InInitializationOrder */
    void *DllBase;
    void *EntryPoint;
    uint32_t SizeOfImage;
    UNICODE_STRING FullDllName;
    UNICODE_STRING BaseDllName;
    uint32_t Flags;
    int16_t LoadCount;
    int16_t TlsIndex;
    LIST_ENTRY HashTableEntry;
    uint32_t TimeDateStamp;
} LDR_DATA_TABLE_ENTRY;

/* Public API */
PEB_LDR_DATA *init_peb_ldr(void);
int ldr_add_module(loaded_module_t *mod);
int ldr_remove_module(loaded_module_t *mod);
LDR_DATA_TABLE_ENTRY *ldr_find_by_addr(void *addr);

/* Global pointer to allocated PEB_LDR_DATA */
extern PEB_LDR_DATA *g_peb_ldr;

#endif /* MY_WINE_PEB_LDR_H */
