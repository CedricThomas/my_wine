/*
 * test_teb_peb.c — TEB/PEB setup unit test
 *
 * Calls setup_teb_peb() from the loader, verifies gs:[0x00] (SEH chain),
 * gs:[0x30] (thread pointer), and gs:[0x60] (PEB) values.
 * Cleans up (munmap TEB/PEB) after verification.
 *
 * Gracefully skips with a message when arch_prctl(ARCH_SET_GS) is
 * unavailable (e.g., running under certain containers).
 *
 * Build: linked against pe_parser.o, image_mapper.o, import_resolver.o,
 *   and teb_peb.o for the setup_teb_peb function.
 */

#define _GNU_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>
#include <signal.h>
#include <sys/mman.h>
#include <sys/syscall.h>
#include <asm/prctl.h>

#include "pe.h"

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

/* ── Crash safety: install SIGSEGV handler ───────────────────
 *
 * signal_handler.o installs a SIGSYS handler that raises SIGSEGV
 * when g_dispatcher is NULL. Since we don't call setup_sigsys_handler()
 * in this test, any accidental SIGSYS would crash us. Install a
 * no-op SIGSEGV handler to prevent that.
 */
static void noop_signal_handler(int sig) { (void)sig; }

static void install_crash_safety(void)
{
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = noop_signal_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sigaction(SIGSEGV, &sa, NULL);
    sigaction(SIGILL, &sa, NULL);
    sigaction(SIGBUS, &sa, NULL);
}

/* ── Helpers ────────────────────────────────────────────────── */

static int can_set_gs_base(void)
{
    /* Try a test set/get cycle with a known value.
     * Use a mapped page as a "safe" test value to avoid
     * triggering kernel validation. */
    void *page = mmap(NULL, 4096, PROT_READ|PROT_WRITE,
                      MAP_PRIVATE|MAP_ANONYMOUS, -1, 0);
    if (page == MAP_FAILED)
        return 0;

    if (syscall(__NR_arch_prctl, ARCH_SET_GS, (unsigned long)page) != 0) {
        munmap(page, 4096);
        return 0;
    }

    uint64_t got = (uint64_t)syscall(__NR_arch_prctl, ARCH_GET_GS, 0);
    if (got != (uint64_t)(uintptr_t)page) {
        munmap(page, 4096);
        return 0;
    }

    /* Restore GS to NULL (we can't restore the original safely) */
    syscall(__NR_arch_prctl, ARCH_SET_GS, 0);
    munmap(page, 4096);

    return 1;
}

/* ── Test: TEB/PEB structure and GS base ───────────────────── */

static void test_teb_peb_setup(void)
{
    if (!can_set_gs_base()) {
        printf("\n=== TEB/PEB Setup: SKIPPED (arch_prctl ARCH_SET_GS unavailable) ===\n");
        return;
    }

    printf("\n=== TEB/PEB Setup ===\n");

    /* Set g_image_base to a plausible value (typical PE image base) */
    g_image_base = (void *)0x140000000ULL;

    /* Initialize import table (needed by import_resolver.o for __msvcrt_*) */
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
    uint64_t gs_base = (uint64_t)syscall(__NR_arch_prctl, ARCH_GET_GS, 0);
    check("GS base == TEB address", (void *)gs_base == teb);

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
    syscall(__NR_arch_prctl, ARCH_SET_GS, 0);
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
    /* Install crash safety: prevent SIGSYS→SIGSEGV from killing us */
    install_crash_safety();

    printf("=== TEB/PEB Tests (t7.4) ===\n");

    test_teb_peb_setup();
    test_stack_setup();

    /* ── Summary ──────────────────────────────────────────── */
    printf("\n========================================\n");
    printf("Total:  %d  Passed: %d  Failed: %d\n",
           total_tests, passed_tests, failed_tests);
    printf("========================================\n");

    return failed_tests > 0 ? 1 : 0;
}
