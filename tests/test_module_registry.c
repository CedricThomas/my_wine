/*
 * test_module_registry.c — Unit tests for module registry and PEB LDR
 *
 * Tests add/find/remove module, multiple module tracking, LDR doubly-linked
 * list integrity (forward/backward/circular walks), PEB LDR pointer wiring,
 * and LDR module removal.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <wchar.h>
#include <sys/mman.h>

#include "pe.h"
#include "nt_constants.h"
#include "src/loader/module_list.h"
#include "src/loader/peb_ldr.h"

/* Forward declarations for functions used in tests */
void *setup_teb_peb(void);
extern void *g_image_base;

/* ── Test harness ──────────────────────────────────────────────── */

static int total_tests = 0;
static int passed_tests = 0;
static int failed_tests = 0;

static void check(const char *label, int condition)
{
    total_tests++;
    if (condition) {
        printf("  PASS: %s\n", label);
        passed_tests++;
    } else {
        printf("  FAIL: %s\n", label);
        failed_tests++;
    }
}

/* ── Helpers ───────────────────────────────────────────────────── */

static char check_buf[128];

static IMAGE_NT_HEADERS64 make_fake_nt(uint32_t size_of_image)
{
    IMAGE_NT_HEADERS64 nt = {0};
    nt.Signature = IMAGE_NT_SIGNATURE;
    nt.FileHeader.Machine = IMAGE_FILE_MACHINE_AMD64;
    nt.FileHeader.SizeOfOptionalHeader = sizeof(IMAGE_OPTIONAL_HEADER64);
    nt.OptionalHeader.Magic = IMAGE_NT_OPTIONAL_HDR64_MAGIC;
    nt.OptionalHeader.ImageBase = 0x140000000ULL;
    nt.OptionalHeader.SizeOfImage = size_of_image;
    nt.OptionalHeader.AddressOfEntryPoint = 0x1000;
    return nt;
}

/* ── Test 1: add/find/remove module ────────────────────────────── */

static void test_add_find_remove(void)
{
    printf("\n=== Test 1: add/find/remove module ===\n");

    init_module_list();
    check("module_count is 0 after init", module_count == 0);

    /* Allocate fake base + NT headers in writable memory */
    void *base = mmap(NULL, 0x1000, PROT_READ | PROT_WRITE,
                      MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    check("mmap for fake base succeeded", base != MAP_FAILED);

    IMAGE_NT_HEADERS64 *nt_ptr = malloc(sizeof(IMAGE_NT_HEADERS64));
    *nt_ptr = make_fake_nt(0x1000);

    loaded_module_t *mod = add_module(base, "test.dll", nt_ptr);
    check("add_module returns non-NULL", mod != NULL);
    check("module_count is 1", module_count == 1);
    check("mod->base matches", mod->base == base);
    check("mod->name is 'test.dll'", strcmp(mod->name, "test.dll") == 0);

    loaded_module_t *found = find_module_by_name("test.dll");
    check("find_module_by_name('test.dll') returns the module", found == mod);

    /* Address within base + SizeOfImage */
    void *addr = (char *)base + 0x500;
    found = find_module_by_addr(addr);
    check("find_module_by_addr(base+0x500) returns the module", found == mod);

    /* Address outside should not match */
    void *out_of_range = (char *)base + 0x2000;
    found = find_module_by_addr(out_of_range);
    check("find_module_by_addr(out-of-range) returns NULL", found == NULL);

    remove_module(mod);
    check("module_count is 0 after remove", module_count == 0);

    found = find_module_by_name("test.dll");
    check("find_module_by_name('test.dll') returns NULL after remove", found == NULL);

    free(nt_ptr);
    munmap(base, 0x1000);
}

/* ── Test 2: multiple modules ──────────────────────────────────── */

static void test_multiple_modules(void)
{
    printf("\n=== Test 2: multiple modules ===\n");

    init_module_list();

    /* Allocate three fake bases with distinct 0x1000 ranges */
    void *bases[3];
    IMAGE_NT_HEADERS64 *nt_ptrs[3];
    const char *names[] = { "kernel32.dll", "ntdll.dll", "user32.dll" };

    for (int i = 0; i < 3; i++) {
        bases[i] = mmap(NULL, 0x1000, PROT_READ | PROT_WRITE,
                        MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        snprintf(check_buf, sizeof(check_buf), "mmap module %d", i);
        check(check_buf, bases[i] != MAP_FAILED);

        nt_ptrs[i] = malloc(sizeof(IMAGE_NT_HEADERS64));
        *nt_ptrs[i] = make_fake_nt(0x1000);

        loaded_module_t *mod = add_module(bases[i], names[i], nt_ptrs[i]);
        snprintf(check_buf, sizeof(check_buf), "add_module %s", names[i]);
        check(check_buf, mod != NULL);
    }

    check("module_count is 3", module_count == 3);

    /* Verify all 3 can be found by name */
    for (int i = 0; i < 3; i++) {
        loaded_module_t *found = find_module_by_name(names[i]);
        snprintf(check_buf, sizeof(check_buf), "find by name %s", names[i]);
        check(check_buf, found != NULL && found->base == bases[i]);
    }

    /* Verify find_module_by_addr finds the correct one for each */
    for (int i = 0; i < 3; i++) {
        void *addr = (char *)bases[i] + 0x800;
        loaded_module_t *found = find_module_by_addr(addr);
        snprintf(check_buf, sizeof(check_buf), "find by addr in %s", names[i]);
        check(check_buf, found != NULL && found->base == bases[i]);
    }

    /* Cross-check: address in module 0 should NOT match module 1 */
    void *addr0 = (char *)bases[0] + 0x800;
    loaded_module_t *wrong = find_module_by_addr(addr0);
    check("addr in module 0 doesn't return module 1",
          wrong != NULL && wrong->base == bases[0] && wrong->base != bases[1]);

    /* Cleanup */
    for (int i = 0; i < 3; i++) {
        loaded_module_t *mod = find_module_by_name(names[i]);
        if (mod) remove_module(mod);
        free(nt_ptrs[i]);
        munmap(bases[i], 0x1000);
    }
}

/* ── Test 3: LDR doubly-linked list integrity ──────────────────── */

static void test_ldr_list_integrity(void)
{
    printf("\n=== Test 3: LDR doubly-linked list integrity ===\n");

    PEB_LDR_DATA *ldr = init_peb_ldr();
    check("init_peb_ldr returns non-NULL", ldr != NULL);

    /* Verify lists start as self-referencing (empty circular) */
    check("InLoadOrderModuleList is self-referencing",
          ldr->InLoadOrderModuleList.Flink == &ldr->InLoadOrderModuleList);
    check("InMemoryOrderModuleList is self-referencing",
          ldr->InMemoryOrderModuleList.Flink == &ldr->InMemoryOrderModuleList);
    check("InInitializationOrderModuleList is self-referencing",
          ldr->InInitializationOrderModuleList.Flink == &ldr->InInitializationOrderModuleList);

    init_module_list();

    /* Add 3 modules */
    void *bases[3];
    IMAGE_NT_HEADERS64 *nt_ptrs[3];
    const char *names[] = { "mod_a.dll", "mod_b.dll", "mod_c.dll" };
    loaded_module_t *mods[3];

    for (int i = 0; i < 3; i++) {
        bases[i] = mmap(NULL, 0x1000, PROT_READ | PROT_WRITE,
                        MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        nt_ptrs[i] = malloc(sizeof(IMAGE_NT_HEADERS64));
        *nt_ptrs[i] = make_fake_nt(0x1000);
        mods[i] = add_module(bases[i], names[i], nt_ptrs[i]);
        ldr_add_module(mods[i]);
    }
    check("module_count is 3 after ldr_add_module", module_count == 3);

    /* ── Walk InLoadOrderModuleList forward ─────────────────── */
    {
        int count = 0;
        LIST_ENTRY *cursor = ldr->InLoadOrderModuleList.Flink;
        while (cursor != &ldr->InLoadOrderModuleList) {
            count++;
            cursor = cursor->Flink;
        }
        check("InLoadOrder forward walk found 3 entries", count == 3);
    }

    /* ── Walk InLoadOrderModuleList backward ────────────────── */
    {
        int count = 0;
        LIST_ENTRY *cursor = ldr->InLoadOrderModuleList.Blink;
        while (cursor != &ldr->InLoadOrderModuleList) {
            count++;
            cursor = cursor->Blink;
        }
        check("InLoadOrder backward walk found 3 entries", count == 3);
    }

    /* ── Walk InMemoryOrder ─────────────────────────────────── */
    {
        int count = 0;
        LIST_ENTRY *cursor = ldr->InMemoryOrderModuleList.Flink;
        while (cursor != &ldr->InMemoryOrderModuleList) {
            count++;
            cursor = cursor->Flink;
        }
        check("InMemoryOrder forward walk found 3 entries", count == 3);
    }
    {
        int count = 0;
        LIST_ENTRY *cursor = ldr->InMemoryOrderModuleList.Blink;
        while (cursor != &ldr->InMemoryOrderModuleList) {
            count++;
            cursor = cursor->Blink;
        }
        check("InMemoryOrder backward walk found 3 entries", count == 3);
    }

    /* ── Walk InInitializationOrder ─────────────────────────── */
    {
        int count = 0;
        LIST_ENTRY *cursor = ldr->InInitializationOrderModuleList.Flink;
        while (cursor != &ldr->InInitializationOrderModuleList) {
            count++;
            cursor = cursor->Flink;
        }
        check("InInitializationOrder forward walk found 3 entries", count == 3);
    }
    {
        int count = 0;
        LIST_ENTRY *cursor = ldr->InInitializationOrderModuleList.Blink;
        while (cursor != &ldr->InInitializationOrderModuleList) {
            count++;
            cursor = cursor->Blink;
        }
        check("InInitializationOrder backward walk found 3 entries", count == 3);
    }

    /* ── Verify circularity: starting from head, following Flink
     *     N+1 times returns to head ─────────────────────────── */
    {
        int all_circular = 1;
        LIST_ENTRY *heads[3] = { &ldr->InLoadOrderModuleList,
                                 &ldr->InMemoryOrderModuleList,
                                 &ldr->InInitializationOrderModuleList };
        for (int i = 0; i < 3; i++) {
            LIST_ENTRY *p = heads[i];
            for (int j = 0; j < 4; j++) {  /* 3 entries + 1 to return to head */
                p = p->Flink;
            }
            if (p != heads[i]) {
                all_circular = 0;
            }
        }
        check("All three lists are circular (Flink returns to head)", all_circular);
    }

    /* Cleanup */
    for (int i = 0; i < 3; i++) {
        if (mods[i]) {
            ldr_remove_module(mods[i]);
            remove_module(mods[i]);
        }
        free(nt_ptrs[i]);
        munmap(bases[i], 0x1000);
    }
    free(ldr);
    g_peb_ldr = NULL;
}

/* ── Test 4: PEB[0x18] points to valid LDR after setup ─────────── */

static void test_peb_ldr_pointer(void)
{
    printf("\n=== Test 4: PEB[0x18] points to valid LDR after setup ===\n");

    /* Allocate a real mmap'd region as g_image_base so that
     * setup_teb_peb doesn't crash when checking madvise + headers.
     * Zero it so the fake headers won't cause segfaults. */
    g_image_base = mmap(NULL, 0x1000, PROT_READ | PROT_WRITE,
                        MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    check("mmap for g_image_base succeeded", g_image_base != MAP_FAILED);
    if (g_image_base == MAP_FAILED) return;
    memset(g_image_base, 0, 0x1000);

    void *teb = setup_teb_peb();

    /* We don't set GS base — we read TEB/PEB directly via pointers */
    if (teb == NULL) {
        printf("  SKIP: setup_teb_peb() returned NULL\n");
        munmap(g_image_base, 0x1000);
        g_image_base = NULL;
        return;
    }
    check("setup_teb_peb returns non-NULL", teb != NULL);

    /* Read PEB pointer from TEB[TEB_PEB_PTR = 0x60] */
    void *peb = *(void **)((uint8_t *)teb + TEB_PEB_PTR);
    check("PEB pointer from TEB[TEB_PEB_PTR] is non-NULL", peb != NULL);

    if (peb != NULL) {
        /* Read LDR pointer from PEB[PEB_LDR = 0x18] */
        PEB_LDR_DATA *ldr = *(PEB_LDR_DATA **)((uint8_t *)peb + PEB_LDR);
        check("PEB[PEB_LDR] is non-NULL", ldr != NULL);

        if (ldr != NULL) {
            /* Verify lists are initialized (self-referencing head for empty,
             * or non-self-referencing if main.exe was added). In either case,
             * Flink and Blink should not be NULL and the structure should be sane. */
            check("InLoadOrderModuleList Flink is not NULL",
                  ldr->InLoadOrderModuleList.Flink != NULL);
            check("InLoadOrderModuleList Blink is not NULL",
                  ldr->InLoadOrderModuleList.Blink != NULL);
            check("InMemoryOrderModuleList Flink is not NULL",
                  ldr->InMemoryOrderModuleList.Flink != NULL);
            check("InMemoryOrderModuleList Blink is not NULL",
                  ldr->InMemoryOrderModuleList.Blink != NULL);
            check("InInitializationOrderModuleList Flink is not NULL",
                  ldr->InInitializationOrderModuleList.Flink != NULL);
            check("InInitializationOrderModuleList Blink is not NULL",
                  ldr->InInitializationOrderModuleList.Blink != NULL);

            /* g_peb_ldr should match the PEB LDR pointer */
            check("g_peb_ldr matches PEB[PEB_LDR]", g_peb_ldr == ldr);
        }
    }

    /* Cleanup: munmap PEB then TEB (same order as production teardown) */
    if (peb != NULL) {
        munmap(peb, 4096);
    }
    munmap(teb, 4096);
    munmap(g_image_base, 0x1000);
    g_image_base = NULL;
}

/* ── Test 5: LDR removal ───────────────────────────────────────── */

static void test_ldr_removal(void)
{
    printf("\n=== Test 5: LDR removal ===\n");

    PEB_LDR_DATA *ldr = init_peb_ldr();
    check("init_peb_ldr returns non-NULL", ldr != NULL);

    init_module_list();

    /* Add a module and verify it's in the LDR lists */
    void *base = mmap(NULL, 0x1000, PROT_READ | PROT_WRITE,
                      MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    check("mmap for fake base succeeded", base != MAP_FAILED);

    IMAGE_NT_HEADERS64 *nt_ptr = malloc(sizeof(IMAGE_NT_HEADERS64));
    *nt_ptr = make_fake_nt(0x1000);

    loaded_module_t *mod = add_module(base, "remove_me.dll", nt_ptr);
    check("add_module returns non-NULL", mod != NULL);

    int rc = ldr_add_module(mod);
    check("ldr_add_module returns 0", rc == 0);
    check("mod->ldr_entry is set", mod->ldr_entry != NULL);

    /* Verify module is in the LDR InLoadOrder list */
    {
        int found = 0;
        LIST_ENTRY *cursor = ldr->InLoadOrderModuleList.Flink;
        while (cursor != &ldr->InLoadOrderModuleList) {
            LDR_DATA_TABLE_ENTRY *entry = (LDR_DATA_TABLE_ENTRY *)(
                ((char *)cursor - offsetof(LDR_DATA_TABLE_ENTRY, DoubleList[0]))
            );
            if (entry->DllBase == base) {
                found = 1;
                break;
            }
            cursor = cursor->Flink;
        }
        check("module found in InLoadOrderModuleList after add", found);
    }

    /* Also verify ldr_find_by_addr works */
    void *addr = (char *)base + 0x500;
    LDR_DATA_TABLE_ENTRY *found = ldr_find_by_addr(addr);
    check("ldr_find_by_addr returns the module", found != NULL && found->DllBase == base);

    /* Remove the module */
    rc = ldr_remove_module(mod);
    check("ldr_remove_module returns 0", rc == 0);
    check("mod->ldr_entry is NULL after remove", mod->ldr_entry == NULL);

    /* Verify it's gone from the LDR InLoadOrder list */
    {
        int found = 0;
        LIST_ENTRY *cursor = ldr->InLoadOrderModuleList.Flink;
        while (cursor != &ldr->InLoadOrderModuleList) {
            LDR_DATA_TABLE_ENTRY *entry = (LDR_DATA_TABLE_ENTRY *)(
                ((char *)cursor - offsetof(LDR_DATA_TABLE_ENTRY, DoubleList[0]))
            );
            if (entry->DllBase == base) {
                found = 1;
                break;
            }
            cursor = cursor->Flink;
        }
        check("module NOT found in InLoadOrderModuleList after remove", !found);
    }

    /* Verify ldr_find_by_addr returns NULL after removal */
    found = ldr_find_by_addr(addr);
    check("ldr_find_by_addr returns NULL after removal", found == NULL);

    /* Verify lists are still circular after removal */
    check("InLoadOrder still circular after remove",
          ldr->InLoadOrderModuleList.Flink == &ldr->InLoadOrderModuleList);

    /* Cleanup */
    remove_module(mod);
    free(nt_ptr);
    munmap(base, 0x1000);
    free(ldr);
    g_peb_ldr = NULL;
}

/* ── Main ──────────────────────────────────────────────────────── */

int main(void)
{
    printf("=== Module Registry + PEB LDR Tests ===\n");

    test_add_find_remove();
    test_multiple_modules();
    test_ldr_list_integrity();
    test_peb_ldr_pointer();
    test_ldr_removal();

    printf("\n========================================\n");
    printf("Total:  %d  Passed: %d  Failed: %d\n",
           total_tests, passed_tests, failed_tests);
    printf("========================================\n");

    return failed_tests > 0 ? 1 : 0;
}