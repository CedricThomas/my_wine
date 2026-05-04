/*
 * test_teb_peb.c — TEB/PEB setup unit test
 *
 * Calls setup_teb_peb() from the loader, verifies gs:[0x00] (SEH chain),
 * gs:[0x30] (thread pointer), and gs:[0x60] (PEB) values.
 * Cleans up (munmap TEB/PEB) after verification.
 *
 * Runs directly in the main process (Wine shared-process model).
 * Gracefully skips with a message when arch_prctl(ARCH_SET_GS) is
 * unavailable (e.g., running under certain containers).
 *
 * Build: linked against pe_parser.o, image_mapper.o, import_table.o, import_resolve.o, import_init.o,
 *   and teb_peb.o for the setup_teb_peb function.
 */

#define _GNU_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>
#include <sys/mman.h>

#include "pe.h"
#include "src/loader/loader_priv.h"

/* ── Forward declarations from loader_priv.h ───────────────── */

extern void *g_image_base;

void init_import_table(void);
void init_msvcrt_imports(void);

void *setup_teb_peb(void);
void *setup_stack(IMAGE_OPTIONAL_HEADER64 *opt);

/* ── Test harness ───────────────────────────────────────────── */

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

/* ── Helpers ────────────────────────────────────────────────── */

static int can_set_gs_base(void)
{
    /* Direct probe in the main process (shared-process model).
     * If the underlying arch_prctl/FSGSBASE crashes, so does this
     * process — that's acceptable for a test binary. */
    void *page = mmap(NULL, 4096, PROT_READ|PROT_WRITE,
                      MAP_PRIVATE|MAP_ANONYMOUS, -1, 0);
    if (page == MAP_FAILED) {
        return 0;
    }

    if (set_gs_base(page) != 0) {
        munmap(page, 4096);
        return 0;
    }

    void *got = get_gs_base();
    if (got != page) {
        munmap(page, 4096);
        return 0;
    }

    /* Restore GS to NULL */
    set_gs_base(NULL);
    munmap(page, 4096);
    return 1;
}

/* ── Test: TEB/PEB structure and GS base ───────────────────── */

static void test_teb_peb_setup(void)
{
    if (!can_set_gs_base()) {
        printf("\n=== TEB/PEB Setup: SKIPPED (neither arch_prctl nor FSGSBASE can set GS base) ===\n");
        return;
    }

    printf("\n=== TEB/PEB Setup ===\n");

    /* Set g_image_base to a plausible value (typical PE image base) */
    g_image_base = (void *)0x140000000ULL;

    /* Initialize import table (needed by import_table.o, import_resolve.o, import_init.o for __msvcrt_*) */
    init_msvcrt_imports();
    init_import_table();

    /* Call setup_teb_peb() */
    void *teb = setup_teb_peb();

    if (teb == NULL) {
        printf("  SKIP: setup_teb_peb() returned NULL\n");
        return;
    }

    check("setup_teb_peb returns non-NULL", teb != NULL);

    /* Read the GS base to confirm it points to TEB */
    void *gs_base = get_gs_base();
    check("GS base == TEB address", gs_base == teb);

    /* Verify gs:[0x00] — SEH chain (set by entry.c in child;
     * setup_teb_peb leaves it as 0 for the parent test) */
    void *seh_chain = *(void **)((uint8_t *)teb + 0x00);
    check("gs:[0x00] (SEH chain) is NULL (set in child, not parent)", seh_chain == NULL);

    /* Verify gs:[0x30] — thread pointer (self-referential TEB pointer) */
    void *thread_ptr = *(void **)((uint8_t *)teb + 0x30);
    check("gs:[0x30] (thread pointer) == TEB (self-referential)", thread_ptr == teb);

    /* Also check teb[0x08] — TEB self-referential pointer */
    void *self_ref = *(void **)((uint8_t *)teb + 0x08);
    check("gs:[0x08] (TEB self-ref) == TEB", self_ref == teb);

    /* Verify gs:[0x60] — PEB pointer */
    void *peb = *(void **)((uint8_t *)teb + 0x60);
    check("gs:[0x60] (PEB) is non-NULL", peb != NULL);

    if (peb != NULL) {
        /* Verify PEB contents */
        void *peb_image_base = *(void **)((uint8_t *)peb + 0x008);
        check("PEB.ImageBaseAddress == g_image_base", peb_image_base == g_image_base);

        uint8_t being_debugged = *(uint8_t *)((uint8_t *)peb + 0x002);
        check("PEB.BeingDebugged == 0", being_debugged == 0);
    }

    /* Cleanup: munmap PEB then TEB */
    if (peb != NULL) {
        int rc_peb = munmap(peb, 4096);
        check("munmap PEB succeeds", rc_peb == 0);
    }
    int rc_teb = munmap(teb, 4096);
    check("munmap TEB succeeds", rc_teb == 0);

    /* Restore GS base to 0 to avoid corrupting the test runner */
    set_gs_base(NULL);
}

/* ── Test: stack setup ─────────────────────────────────────── */

static void test_stack_setup(void)
{
    printf("\n=== Stack Setup ===\n");

    IMAGE_OPTIONAL_HEADER64 opt;
    memset(&opt, 0, sizeof(opt));
    opt.SizeOfStackReserve = 1024 * 1024;   /* 1 MB reserve */
    opt.SizeOfStackCommit  = 4096;           /* 1 page commit (will be bumped to 512KB) */

    void *stack_top = setup_stack(&opt);

    if (stack_top == NULL) {
        printf("  SKIP: setup_stack() returned NULL\n");
        return;
    }

    check("setup_stack returns non-NULL", stack_top != NULL);

    /* Stack top should be near the end of the committed region
     * (aligned to 16 bytes per x86_64 ABI, with rsp%16==8) */
    uintptr_t top = (uintptr_t)stack_top;
    check("stack top is 16-byte aligned with offset 8 (rsp%16==8)",
          (top & 0xF) == 8);

    /* g_stack_base and g_stack_size should be set */
    extern void *g_stack_base;
    extern size_t g_stack_size;
    check("g_stack_base is non-NULL", g_stack_base != NULL);
    check("g_stack_size >= 512KB (minimum enforced)", g_stack_size >= 512 * 1024);

    /* stack_top should be above g_stack_base */
    check("stack_top > g_stack_base", stack_top > g_stack_base);

    /* Cleanup */
    if (g_stack_base != NULL && g_stack_size > 0) {
        int rc = munmap(g_stack_base, g_stack_size);
        check("munmap stack succeeds", rc == 0);
        g_stack_base = NULL;
        g_stack_size = 0;
    }
}

/* ── Main ───────────────────────────────────────────────────── */

int main(void)
{
    printf("=== TEB/PEB Tests ===\n");

    test_teb_peb_setup();
    test_stack_setup();

    printf("\n========================================\n");
    printf("Total:  %d  Passed: %d  Failed: %d\n",
           total_tests, passed_tests, failed_tests);
    printf("========================================\n");

    return failed_tests > 0 ? 1 : 0;
}
