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

/* ── UNICODE_STRING (PEB/LDR variant) ───────────────────────────── */
/*
 * Our LDR structures use 8-byte aligned wchar_t* buffers (16-byte struct),
 * matching the Windows x64 layout in PEB_LDR_DATA / LDR_DATA_TABLE_ENTRY.
 * This differs from include/ntdll.h which defines UNICODE_STRING with
 * uint64_t Buffer (packed, 12 bytes) for raw syscall structs.
 */
typedef struct {
    uint16_t Length;
    uint16_t MaximumLength;
    wchar_t *Buffer;
} PEB_UNICODE_STRING;

/* ── PEB_LDR_DATA ──────────────────────────────────────────────── */
typedef struct peb_ldr_data {
    uint8_t Reserved[8];
    LIST_ENTRY InLoadOrderModuleList;
    LIST_ENTRY InMemoryOrderModuleList;
    LIST_ENTRY InInitializationOrderModuleList;
    uint8_t Reserved2[32];   /* Windows x64: 0x38–0x57, total 0x58 */
} PEB_LDR_DATA;

/* ── LDR_DATA_TABLE_ENTRY ───────────────────────────────────────── */
/*
 * Windows x64 layout (up to 0x80):
 *   0x00  DoubleList[3]  (3 × LIST_ENTRY, 0x30 bytes)
 *   0x30  DllBase
 *   0x38  EntryPoint
 *   0x40  SizeOfImage
 *   0x48  FullDllName    (UNICODE_STRING, 16 bytes)
 *   0x58  BaseDllName    (UNICODE_STRING, 16 bytes)
 *   0x68  Flags
 *   0x6C  LoadCount
 *   0x6E  TlsIndex
 *   0x70  HashTableEntry (LIST_ENTRY, 16 bytes)
 *   0x80  TimeDateStamp
 *
 * Windows x64 total size is 0xB8 (184 bytes). Fields after 0x80
 * (SectionPointer, CheckSum, LoadTime, etc.) vary by Windows version.
 * We pad to 0xB8 for compatibility.
 */
typedef struct ldr_data_table_entry {
    LIST_ENTRY DoubleList[3];  /* [0]=InLoadOrder, [1]=InMemoryOrder, [2]=InInitializationOrder */
    void *DllBase;
    void *EntryPoint;
    uint32_t SizeOfImage;
    PEB_UNICODE_STRING FullDllName;
    PEB_UNICODE_STRING BaseDllName;
    uint32_t Flags;
    int16_t LoadCount;
    int16_t TlsIndex;
    LIST_ENTRY HashTableEntry;
    uint32_t TimeDateStamp;
    uint8_t  Padding[0x34];   /* 0x84–0xB7: Windows fields (SectionPointer, etc.) vary by version */
} LDR_DATA_TABLE_ENTRY;

/* Public API */
PEB_LDR_DATA *init_peb_ldr(void);
int ldr_add_module(loaded_module_t *mod);
int ldr_remove_module(loaded_module_t *mod);
LDR_DATA_TABLE_ENTRY *ldr_find_by_addr(void *addr);

/* Global pointer to allocated PEB_LDR_DATA */
extern PEB_LDR_DATA *g_peb_ldr;

#endif /* MY_WINE_PEB_LDR_H */
