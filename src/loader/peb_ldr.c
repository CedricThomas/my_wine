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
#include <sys/mman.h>

#include "peb_ldr.h"
#include "image_mapper.h"
#include "include/common.h"
#include "loader_utils.h"
#include "loader_state.h"
#include "../syscall/syscalls_inline.h"

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
    PEB_LDR_DATA *ldr;
    if (loader_is_32bit()) {
        /* For PE32, allocate below 4GB so the truncated pointer is valid */
        ldr = (PEB_LDR_DATA *)INLINE_SYSCALL_MMAP(NULL, sizeof(PEB_LDR_DATA),
                    PROT_READ | PROT_WRITE,
                    MAP_PRIVATE | MAP_ANONYMOUS | MAP_32BIT, -1, 0);
        if (ldr == (void *)-1 || ldr == NULL) return NULL;
    } else {
        ldr = (PEB_LDR_DATA *)malloc(sizeof(PEB_LDR_DATA));
        if (!ldr) return NULL;
    }

    memset(ldr, 0, sizeof(PEB_LDR_DATA));

    list_init(&ldr->InLoadOrderModuleList);
    list_init(&ldr->InMemoryOrderModuleList);
    list_init(&ldr->InInitializationOrderModuleList);

    loader_set_peb_ldr(ldr);
    return ldr;
}

int ldr_add_module(loaded_module_t *mod)
{
    if (!mod || !mod->base || !mod->nt || !(PEB_LDR_DATA *)g_loader.peb_ldr)
        return -1;

    if (mod->ldr_linked)
        return 0;  /* Already linked */

    LDR_DATA_TABLE_ENTRY *entry = &mod->ldr_entry;

    /* Update fields that may have changed since add_module populated them.
     * The entry was already initialized by add_module with DllBase,
     * EntryPoint, SizeOfImage, FullDllName, BaseDllName, TimeDateStamp,
     * and self-referencing DoubleList nodes. */
    entry->LoadCount = mod->load_count;

    PEB_LDR_DATA *ldr = (PEB_LDR_DATA *)g_loader.peb_ldr;
    /* Insert into the three PEB LDR lists */
    list_insert_tail(&ldr->InLoadOrderModuleList, &entry->DoubleList[0]);
    list_insert_tail(&ldr->InMemoryOrderModuleList, &entry->DoubleList[1]);
    list_insert_tail(&ldr->InInitializationOrderModuleList, &entry->DoubleList[2]);

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
    dll_memset(entry, 0, sizeof(LDR_DATA_TABLE_ENTRY));

    mod->ldr_linked = 0;

    return 0;
}

LDR_DATA_TABLE_ENTRY *ldr_find_by_addr(void *addr)
{
    LIST_ENTRY *cursor;

    if (!g_loader.peb_ldr || !addr)
        return NULL;

    PEB_LDR_DATA *ldr = (PEB_LDR_DATA *)g_loader.peb_ldr;
    LIST_ENTRY *cursor = ldr->InMemoryOrderModuleList.Flink;

    while (cursor != &ldr->InMemoryOrderModuleList) {
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
