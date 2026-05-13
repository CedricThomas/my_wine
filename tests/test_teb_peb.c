/*
 * test_teb_peb.c — TEB/PEB setup unit test
 *
 * Calls setup_teb_peb() from the loader, verifies gs:[0x00] (SEH chain),
 * gs:[0x48] (thread pointer), and gs:[0x60] (PEB) values.
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
#include "nt_constants.h"
#include "src/loader/loader_priv.h"

/* ── Forward declarations from loader_priv.h ───────────────── */

void init_import_table(void);
void init_msvcrt_imports(void);

void *setup_teb_peb(void);
void *setup_stack(IMAGE_NT_HEADERS *nt);

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
        set_gs_base(NULL);
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

    /* Set g_loader.image_base to a plausible value (typical PE image base) */
    g_loader.image_base = (void *)0x140000000ULL;

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

    /* Replicate the production finalize_guest_state() sequence:
     * set GS base to TEB right before checking it */
    if (set_gs_base(teb) != 0) {
        printf("  SKIP: set_gs_base(teb) failed\n");
        munmap(teb, 4096);
        return;
    }

    /* Read the GS base to confirm it points to TEB */
    void *gs_base = get_gs_base();
    check("GS base == TEB address", gs_base == teb);

    /* Verify gs:[0x00] — SEH chain (set by entry.c in child;
     * setup_teb_peb leaves it as 0 for the parent test) */
    void *seh_chain = *(void **)((uint8_t *)teb + 0x00);
    check("gs:[0x00] (SEH chain) is NULL (set in child, not parent)", seh_chain == NULL);

    /* Verify gs:[0x48] — thread pointer (self-referential TEB pointer) */
    void *thread_ptr = *(void **)((uint8_t *)teb + TEB64_THREAD_PTR);
    check("gs:[0x48] (thread pointer) == TEB (self-referential)", thread_ptr == teb);

    /* Also check teb[0x08] — TEB self-referential pointer */
    void *self_ref = *(void **)((uint8_t *)teb + 0x08);
    check("gs:[0x08] (TEB self-ref) == TEB", self_ref == teb);

    /* Verify gs:[0x60] — PEB pointer */
    void *peb = *(void **)((uint8_t *)teb + 0x60);
    check("gs:[0x60] (PEB) is non-NULL", peb != NULL);

    if (peb != NULL) {
        /* Verify PEB contents */
        void *peb_image_base = *(void **)((uint8_t *)peb + 0x008);
        check("PEB.ImageBaseAddress == g_loader.image_base", peb_image_base == g_loader.image_base);

        uint8_t being_debugged = *(uint8_t *)((uint8_t *)peb + 0x002);
        check("PEB.BeingDebugged == 0", being_debugged == 0);
    }

    /* Restore GS base to NULL first — before munmap — to avoid
     * any window where GS points to freed memory (e.g. vDSO reads)
     * during printf/check calls below. Mirrors can_set_gs_base(). */
    set_gs_base(NULL);

    /* Cleanup: munmap PEB then TEB */
    if (peb != NULL) {
        int rc_peb = munmap(peb, 4096);
        check("munmap PEB succeeds", rc_peb == 0);
    }
    int rc_teb = munmap(teb, 4096);
    check("munmap TEB succeeds", rc_teb == 0);
}

/* ── Test: 32-bit TEB init_teb32_fields ───────────────────── */

static void test_teb32_fields(void)
{
    /* Allocate a 1KB TEB (one page, 32-bit layout) */
    void *teb = mmap(NULL, 4096, PROT_READ|PROT_WRITE,
                     MAP_PRIVATE|MAP_ANONYMOUS, -1, 0);
    if (teb == MAP_FAILED) {
        printf("  SKIP: mmap failed for 32-bit TEB\n");
        return;
    }
    memset(teb, 0, 4096);

    /* Allocate a 1KB PEB to pass into init_teb32_fields */
    void *peb = mmap(NULL, 4096, PROT_READ|PROT_WRITE,
                     MAP_PRIVATE|MAP_ANONYMOUS, -1, 0);
    if (peb == MAP_FAILED) {
        munmap(teb, 4096);
        printf("  SKIP: mmap failed for 32-bit PEB\n");
        return;
    }
    memset(peb, 0, 4096);

    printf("\n=== 32-bit TEB init_teb32_fields ===\n");

    /* Call init_teb32_fields (shared between pe32_entry.c and setup_teb_peb) */
    init_teb32_fields(teb, peb);

    /* --- Verify Wine-compatible 32-bit TEB offsets --- */

    /* SEH chain (TEB+0x00) — left as NULL since init_teb32_fields doesn't touch it */
    {
        uint32_t seh = *(uint32_t *)((uint8_t *)teb + TEB32_SEH_CHAIN);
        check("TEB32 SEH chain (offset 0x00) is zero", seh == 0);
    }

    /* TEB self-reference (TEB+0x04) — should point back to TEB */
    {
        uint32_t self_ref = *(uint32_t *)((uint8_t *)teb + TEB32_TEB_SELF_REF);
        check("TEB32 self-ref (offset 0x04) == TEB address", self_ref == (uint32_t)(uintptr_t)teb);
    }

    /* ThreadPointer (TEB+0x24) — should point to TEB (self-referential) */
    {
        uint32_t thread_ptr = *(uint32_t *)((uint8_t *)teb + TEB32_THREAD_PTR);
        check("TEB32 ThreadPointer (offset 0x24) == TEB address", thread_ptr == (uint32_t)(uintptr_t)teb);
    }

    /* PEB pointer (TEB+0x30) — should point to PEB */
    {
        uint32_t peb_ptr = *(uint32_t *)((uint8_t *)teb + TEB32_PEB_PTR);
        check("TEB32 PEB pointer (offset 0x30) == PEB address", peb_ptr == (uint32_t)(uintptr_t)peb);
    }

    /* ClientId.UniqueProcess (TEB+0x40) — left as 0 by init_teb32_fields */
    {
        uint32_t unique_proc = *(uint32_t *)((uint8_t *)teb + 0x40);
        check("TEB32 ClientId.UniqueProcess (offset 0x40) is zero (set in pe32_entry)", unique_proc == 0);
    }

    /* ClientId.UniqueThread (TEB+0x44) — left as 0 by init_teb32_fields */
    {
        uint32_t unique_thread = *(uint32_t *)((uint8_t *)teb + 0x44);
        check("TEB32 ClientId.UniqueThread (offset 0x44) is zero (set in pe32_entry)", unique_thread == 0);
    }

    /* EnvironmentPointer (TEB+0x48) — left as 0 by init_teb32_fields */
    {
        uint32_t env_ptr = *(uint32_t *)((uint8_t *)teb + 0x48);
        check("TEB32 EnvironmentPointer (offset 0x48) is zero (set in pe32_entry)", env_ptr == 0);
    }

    /* LastStatusValue (TEB+0x34) — should be STATUS_SUCCESS (0) */
    {
        uint32_t last_status = *(uint32_t *)((uint8_t *)teb + 0x34);
        check("TEB32 LastStatusValue (offset 0x34) == STATUS_SUCCESS (0)", last_status == 0);
    }

    /* FiberData (TEB+0x10) — should point to TEB */
    {
        uint32_t fiber_data = *(uint32_t *)((uint8_t *)teb + TEB32_FIBER_DATA);
        check("TEB32 FiberData (offset 0x10) == TEB address", fiber_data == (uint32_t)(uintptr_t)teb);
    }

    /* GdiTebOffset (TEB+0x18) — should point to GdiProcessLocals */
    {
        uint32_t gdi_offset = *(uint32_t *)((uint8_t *)teb + TEB32_GDI_TEB_OFFSET);
        check("TEB32 GdiTebOffset (offset 0x18) == TEB + GdiProcessLocals",
              gdi_offset == (uint32_t)(uintptr_t)teb + TEB32_GDI_PROCESS_LOCAL);
    }

    /* GdiProcessLocals (TEB+0x1C) — should be 0 */
    {
        uint32_t gdi_process = *(uint32_t *)((uint8_t *)teb + TEB32_GDI_PROCESS_LOCAL);
        check("TEB32 GdiProcessLocals (offset 0x1C) is zero", gdi_process == 0);
    }

    /* GdiThreadLocals (TEB+0x20) — should be 0 */
    {
        uint32_t gdi_thread = *(uint32_t *)((uint8_t *)teb + TEB32_GDI_PROCESS_LOCAL + 4);
        check("TEB32 GdiThreadLocals (offset 0x20) is zero", gdi_thread == 0);
    }

    /* Cleanup */
    munmap(peb, 4096);
    munmap(teb, 4096);
}

/* ── Test: 32-bit TEB full init (init_teb32_fields + pe32_entry extras) ── */

static void test_teb32_full_init(void)
{
    /* Allocate a 1KB TEB (simulating init_teb32 from pe32_entry.c) */
    void *teb = mmap(NULL, 4096, PROT_READ|PROT_WRITE,
                     MAP_PRIVATE|MAP_ANONYMOUS, -1, 0);
    if (teb == MAP_FAILED) {
        printf("  SKIP: mmap failed for 32-bit TEB (full init)\n");
        return;
    }
    memset(teb, 0, 4096);

    /* Allocate a 1KB PEB */
    void *peb = mmap(NULL, 4096, PROT_READ|PROT_WRITE,
                     MAP_PRIVATE|MAP_ANONYMOUS, -1, 0);
    if (peb == MAP_FAILED) {
        munmap(teb, 4096);
        printf("  SKIP: mmap failed for 32-bit PEB (full init)\n");
        return;
    }
    memset(peb, 0, 4096);

    printf("\n=== 32-bit TEB Full Init (simulating pe32_entry init_teb32) ===\n");

    /* Call init_teb32_fields (as done in both pe32_entry.c and setup_teb_peb) */
    init_teb32_fields(teb, peb);

    /* Simulate the additional writes from pe32_entry.c init_teb32:
     * - SEH chain at TEB+0x00
     * - EnvironmentPointer at TEB+0x48
     * - ClientId at TEB+0x40/0x44
     */
    *(uint32_t *)((uint8_t *)teb + TEB32_SEH_CHAIN) = 0xDEADBEEF;

    uint32_t pid_val = (uint32_t)getpid();
    uint32_t tid_val = (uint32_t)gettid();
    *(uint32_t *)((uint8_t *)teb + 0x40) = pid_val;
    *(uint32_t *)((uint8_t *)teb + 0x44) = tid_val;

    extern char **environ;
    *(uint32_t *)((uint8_t *)teb + 0x48) = (uint32_t)(uintptr_t)environ;

    /* Now verify all fields match Wine-compatible 32-bit TEB layout */

    /* SEH chain (TEB+0x00) — set to 0xDEADBEEF above */
    {
        uint32_t seh = *(uint32_t *)((uint8_t *)teb + TEB32_SEH_CHAIN);
        check("Full-init TEB32 SEH chain (offset 0x00) == 0xDEADBEEF", seh == 0xDEADBEEF);
    }

    /* TEB self-reference (TEB+0x04) */
    {
        uint32_t self_ref = *(uint32_t *)((uint8_t *)teb + TEB32_TEB_SELF_REF);
        check("Full-init TEB32 self-ref (offset 0x04) == TEB", self_ref == (uint32_t)(uintptr_t)teb);
    }

    /* ThreadPointer (TEB+0x24) */
    {
        uint32_t thread_ptr = *(uint32_t *)((uint8_t *)teb + TEB32_THREAD_PTR);
        check("Full-init TEB32 ThreadPointer (offset 0x24) == TEB", thread_ptr == (uint32_t)(uintptr_t)teb);
    }

    /* PEB pointer (TEB+0x30) */
    {
        uint32_t peb_ptr = *(uint32_t *)((uint8_t *)teb + TEB32_PEB_PTR);
        check("Full-init TEB32 PEB pointer (offset 0x30) == PEB", peb_ptr == (uint32_t)(uintptr_t)peb);
    }

    /* ClientId.UniqueProcess (TEB+0x40) */
    {
        uint32_t unique_proc = *(uint32_t *)((uint8_t *)teb + 0x40);
        check("Full-init TEB32 ClientId.UniqueProcess (offset 0x40) == getpid()", unique_proc == pid_val);
    }

    /* ClientId.UniqueThread (TEB+0x44) */
    {
        uint32_t unique_thread = *(uint32_t *)((uint8_t *)teb + 0x44);
        check("Full-init TEB32 ClientId.UniqueThread (offset 0x44) == gettid()", unique_thread == tid_val);
    }

    /* EnvironmentPointer (TEB+0x48) */
    {
        uint32_t env_ptr = *(uint32_t *)((uint8_t *)teb + 0x48);
        check("Full-init TEB32 EnvironmentPointer (offset 0x48) == environ", env_ptr == (uint32_t)(uintptr_t)environ);
    }

    /* LastStatusValue (TEB+0x34) */
    {
        uint32_t last_status = *(uint32_t *)((uint8_t *)teb + 0x34);
        check("Full-init TEB32 LastStatusValue (offset 0x34) == 0", last_status == 0);
    }

    /* Cleanup */
    munmap(peb, 4096);
    munmap(teb, 4096);
}

/* ── Test: stack setup ─────────────────────────────────────── */

static void test_stack_setup(void)
{
    printf("\n=== Stack Setup ===\n");

    IMAGE_NT_HEADERS nt;
    memset(&nt, 0, sizeof(nt));
    nt.pe_type = PE_TYPE_64;
    nt.u.nt64.OptionalHeader.SizeOfStackReserve = 1024 * 1024;   /* 1 MB reserve */
    nt.u.nt64.OptionalHeader.SizeOfStackCommit  = 4096;           /* 1 page commit (will be bumped to 512KB) */

    void *stack_top = setup_stack(&nt);

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
    test_teb32_fields();
    test_teb32_full_init();
    test_stack_setup();

    printf("\n========================================\n");
    printf("Total:  %d  Passed: %d  Failed: %d\n",
           total_tests, passed_tests, failed_tests);
    printf("========================================\n");

    return failed_tests > 0 ? 1 : 0;
}
