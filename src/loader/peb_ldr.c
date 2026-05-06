/*
 * peb_ldr.c — PEB LDR data structures management
 *
 * Manages the PEB_LDR_DATA structure and LDR_DATA_TABLE_ENTRY nodes
 * (embedded in loaded_module_t) for each loaded module.
 *
 * Only init_peb_ldr() uses malloc (called on host stack).
 * ldr_add_module/ldr_remove_module operate on embedded structures.
 */

#include <stdlib.h>
#include <string.h>

#include "peb_ldr.h"

/* ── Global state ──────────────────────────────────────────────── */
PEB_LDR_DATA *g_peb_ldr = NULL;

/* ── Hand-rolled helpers (no glibc for guest-stack safety) ─────── */

static size_t pdr_strlen(const char *s)
{
    size_t len = 0;
    while (s[len]) len++;
    return len;
}

static void pdr_memset(void *ptr, int c, size_t n)
{
    uint8_t *p = (uint8_t *)ptr;
    size_t i;
    for (i = 0; i < n; i++)
        p[i] = (uint8_t)c;
}

/* Copy ASCII string into an embedded LDR_UNICODE_STRING buffer */
static void pdr_make_unicode_string(LDR_UNICODE_STRING *us, const char *src)
{
    size_t len = pdr_strlen(src);
    size_t i;

    pdr_memset(us->Buffer, 0, sizeof(us->Buffer));

    for (i = 0; i < len && i < MAX_LDR_NAME_WCHAR; i++) {
        us->Buffer[i] = (uint16_t)(uint8_t)src[i];
    }

    us->Length = (uint16_t)(len * sizeof(uint16_t));
    us->MaximumLength = (uint16_t)((len + 1) * sizeof(uint16_t));
}

/* ── List helpers ───────────────────────────────────────────────── */

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

/* ── Public API ─────────────────────────────────────────────────── */

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
    int i;

    if (!mod || !mod->base || !mod->nt || !g_peb_ldr)
        return -1;

    if (mod->ldr_linked)
        return 0;  /* Already linked */

    LDR_DATA_TABLE_ENTRY *entry = &mod->ldr_entry;

    /* Zero the entry */
    pdr_memset(entry, 0, sizeof(LDR_DATA_TABLE_ENTRY));

    entry->DllBase = mod->base;
    entry->SizeOfImage = mod->nt->OptionalHeader.SizeOfImage;
    entry->EntryPoint = (char *)mod->base + mod->nt->OptionalHeader.AddressOfEntryPoint;
    entry->TimeDateStamp = mod->nt->FileHeader.TimeDateStamp;
    entry->LoadCount = 1;

    pdr_make_unicode_string(&entry->FullDllName, mod->name);
    pdr_make_unicode_string(&entry->BaseDllName, mod->name);

    /* Initialize and insert each of the three list nodes */
    for (i = 0; i < 3; i++) {
        list_init(&entry->DoubleList[i]);
    }

    list_insert_tail(&g_peb_ldr->InLoadOrderModuleList, &entry->DoubleList[0]);
    list_insert_tail(&g_peb_ldr->InMemoryOrderModuleList, &entry->DoubleList[1]);
    list_insert_tail(&g_peb_ldr->InInitializationOrderModuleList, &entry->DoubleList[2]);

    list_init(&entry->HashTableEntry);

    mod->ldr_linked = 1;

    return 0;
}

int ldr_remove_module(loaded_module_t *mod)
{
    if (!mod || !mod->ldr_linked)
        return 0;

    LDR_DATA_TABLE_ENTRY *entry = &mod->ldr_entry;

    /* Unlink from all three lists */
    list_remove(&entry->DoubleList[0]);
    list_remove(&entry->DoubleList[1]);
    list_remove(&entry->DoubleList[2]);

    /* Reset the entry */
    pdr_memset(entry, 0, sizeof(LDR_DATA_TABLE_ENTRY));

    mod->ldr_linked = 0;

    return 0;
}

LDR_DATA_TABLE_ENTRY *ldr_find_by_addr(void *addr)
{
    LIST_ENTRY *cursor;

    if (!g_peb_ldr || !addr)
        return NULL;

    cursor = g_peb_ldr->InMemoryOrderModuleList.Flink;

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
