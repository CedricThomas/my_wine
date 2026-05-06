/*
 * peb_ldr.c — PEB LDR data structures management
 *
 * Manages the PEB_LDR_DATA structure and LDR_DATA_TABLE_ENTRY nodes
 * for each loaded module, maintaining the three Windows-style
 * doubly-linked lists: InLoadOrder, InMemoryOrder, InInitializationOrder.
 */

#include <stdlib.h>
#include <string.h>
#include <wchar.h>

#include "peb_ldr.h"
#include "module_list.h"

/* ── Global state ──────────────────────────────────────────────── */
PEB_LDR_DATA *g_peb_ldr = NULL;

/* ── List helpers ──────────────────────────────────────────────── */

/* Initialize a LIST_ENTRY as a self-referencing head node */
static void list_init(LIST_ENTRY *entry)
{
    entry->Flink = entry;
    entry->Blink = entry;
}

/* Insert entry just before head (i.e., at the tail of the circular list) */
static void list_insert_tail(LIST_ENTRY *head, LIST_ENTRY *entry)
{
    LIST_ENTRY *prev = head->Blink;

    entry->Flink = head;
    entry->Blink = prev;
    prev->Flink = entry;
    head->Blink = entry;
}

/* Unlink entry from its list (entry must not be the head) */
static void list_remove(LIST_ENTRY *entry)
{
    entry->Blink->Flink = entry->Flink;
    entry->Flink->Blink = entry->Blink;
    entry->Flink = entry;
    entry->Blink = entry;
}

/* ── UTF-16 conversion helpers ─────────────────────────────────── */

/* Convert an ASCII string to a wchar_t (UTF-16) string.
 * Returns a malloc'd buffer that must be freed by the caller. */
static wchar_t *ascii_to_utf16(const char *src)
{
    size_t len = strlen(src);
    wchar_t *buf = (wchar_t *)malloc((len + 1) * sizeof(wchar_t));
    if (!buf) return NULL;

    for (size_t i = 0; i <= len; i++) {
        buf[i] = (wchar_t)(uint8_t)src[i];
    }
    return buf;
}

/* Create a UNICODE_STRING from an ASCII source.
 * Buffers are malloc'd and must be freed by the caller. */
static UNICODE_STRING create_unicode_string(const char *src)
{
    UNICODE_STRING us;
    size_t len = strlen(src);
    us.Buffer = ascii_to_utf16(src);
    if (!us.Buffer) {
        us.Length = 0;
        us.MaximumLength = 0;
        us.Buffer = NULL;
        return us;
    }
    us.Length = (uint16_t)(len * sizeof(wchar_t));
    us.MaximumLength = (uint16_t)((len + 1) * sizeof(wchar_t));
    return us;
}

/* ── Public API ────────────────────────────────────────────────── */

PEB_LDR_DATA *init_peb_ldr(void)
{
    PEB_LDR_DATA *ldr = (PEB_LDR_DATA *)malloc(sizeof(PEB_LDR_DATA));
    if (!ldr) return NULL;

    memset(ldr, 0, sizeof(PEB_LDR_DATA));

    list_init(&ldr->InLoadOrderModuleList);
    list_init(&ldr->InMemoryOrderModuleList);
    list_init(&ldr->InInitializationOrderModuleList);

    g_peb_ldr = ldr;
    return ldr;
}

int ldr_add_module(loaded_module_t *mod)
{
    if (!mod || !mod->base || !mod->nt || !g_peb_ldr)
        return -1;

    LDR_DATA_TABLE_ENTRY *entry = (LDR_DATA_TABLE_ENTRY *)calloc(1, sizeof(LDR_DATA_TABLE_ENTRY));
    if (!entry)
        return -1;

    entry->DllBase = mod->base;
    entry->SizeOfImage = mod->nt->OptionalHeader.SizeOfImage;
    entry->EntryPoint = (char *)mod->base + mod->nt->OptionalHeader.AddressOfEntryPoint;
    entry->TimeDateStamp = mod->nt->FileHeader.TimeDateStamp;

    entry->FullDllName = create_unicode_string(mod->name);
    if (!entry->FullDllName.Buffer) {
        free(entry);
        return -1;
    }

    entry->BaseDllName = create_unicode_string(mod->name);
    if (!entry->BaseDllName.Buffer) {
        free(entry->FullDllName.Buffer);
        free(entry);
        return -1;
    }

    /* Initialize and insert each of the three list nodes */
    for (int i = 0; i < 3; i++) {
        list_init(&entry->DoubleList[i]);
    }

    list_insert_tail(&g_peb_ldr->InLoadOrderModuleList, &entry->DoubleList[0]);
    list_insert_tail(&g_peb_ldr->InMemoryOrderModuleList, &entry->DoubleList[1]);
    list_insert_tail(&g_peb_ldr->InInitializationOrderModuleList, &entry->DoubleList[2]);

    list_init(&entry->HashTableEntry);

    entry->LoadCount = 1;

    mod->ldr_entry = entry;

    return 0;
}

int ldr_remove_module(loaded_module_t *mod)
{
    if (!mod || !mod->ldr_entry)
        return 0;

    LDR_DATA_TABLE_ENTRY *entry = mod->ldr_entry;

    /* Unlink from all three lists */
    list_remove(&entry->DoubleList[0]);
    list_remove(&entry->DoubleList[1]);
    list_remove(&entry->DoubleList[2]);

    /* Free the Unicode string buffers */
    free(entry->FullDllName.Buffer);
    free(entry->BaseDllName.Buffer);

    free(entry);

    mod->ldr_entry = NULL;

    return 0;
}

LDR_DATA_TABLE_ENTRY *ldr_find_by_addr(void *addr)
{
    if (!g_peb_ldr || !addr)
        return NULL;

    LIST_ENTRY *cursor = g_peb_ldr->InMemoryOrderModuleList.Flink;

    while (cursor != &g_peb_ldr->InMemoryOrderModuleList) {
        LDR_DATA_TABLE_ENTRY *entry = (LDR_DATA_TABLE_ENTRY *)(
            ((char *)cursor - offsetof(LDR_DATA_TABLE_ENTRY, DoubleList[1]))
        );

        uint8_t *base = (uint8_t *)entry->DllBase;
        uint8_t *ptr  = (uint8_t *)addr;

        if (ptr >= base && ptr < base + entry->SizeOfImage)
            return entry;

        cursor = cursor->Flink;
    }

    return NULL;
}
