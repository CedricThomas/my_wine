/*
 * test_syscall_dispatch.c — Syscall dispatch unit tests
 *
 * Creates mock ucontext_t structures with known register values,
 * calls handle_syscall() for each supported syscall, and verifies
 * that the correct handler is invoked and the return value is set
 * in RAX.
 *
 * Tests safe syscalls that do not call exit() or dereference
 * guest pointers that would segfault.
 *
 * Build: linked against dispatcher.o and all
 *   ntdll handler .o files.
 */

#define _GNU_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <signal.h>
#include <sys/ucontext.h>

#include "ntdll.h"
#include "syscall/dispatcher.h"

/* ── Forward declaration for handle table init ───────────────── */
extern void init_handle_table(void);

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

/* ── Crash safety ─────────────────────────────────────────────
 *
 * Install no-op handlers for crash safety in case any handler
 * raises SIGSEGV during testing.
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

static ucontext_t create_mock_context(void)
{
    ucontext_t ctx;
    memset(&ctx, 0, sizeof(ctx));
    return ctx;
}

/*
 * Test a syscall and verify the result.
 */
static int test_syscall_one(uint64_t syscall_num, const char *label,
                            ucontext_t *ctx, uint64_t expected_rax)
{
    printf("  Testing %s (0x%lx)...\n", label, (unsigned long)syscall_num);

    int rc = handle_syscall(syscall_num, ctx);

    greg_t rax = ctx->uc_mcontext.gregs[REG_RAX];

    check("handle_syscall returns 0 (success dispatch)", rc == 0);
    check("RAX set to expected value", (uint64_t)rax == expected_rax);

    return 0;
}

/* ── Test: NtCallbackReturn (0x05) ─────────────────────────── */

static void test_nt_callback_return(void)
{
    printf("\n--- NtCallbackReturn (0x05) ---\n");

    ucontext_t ctx = create_mock_context();
    /* NtCallbackReturn takes no arguments; registers don't matter */
    test_syscall_one(0x05, "NtCallbackReturn", &ctx, STATUS_SUCCESS);
}

/* ── Test: NtClose (0x0F) ──────────────────────────────────── */

static void test_nt_close(void)
{
    printf("\n--- NtClose (0x0F) ---\n");

    /* Test with valid handle: STDIN_HANDLE = 0x7FFFFFFF
     * (special-cased in handler — not actually closed) */
    ucontext_t ctx = create_mock_context();
    ctx.uc_mcontext.gregs[REG_RCX] = 0x7FFFFFFFUL;  /* STDIN_HANDLE */
    test_syscall_one(0x0F, "NtClose (stdin)", &ctx, STATUS_SUCCESS);

    /* Test with invalid handle */
    ctx = create_mock_context();
    ctx.uc_mcontext.gregs[REG_RCX] = 0xDEAD;  /* not in handle table */
    int rc = handle_syscall(0x0F, &ctx);
    check("NtClose(invalid) returns STATUS_INVALID_HANDLE",
          rc == 0 && (uint64_t)ctx.uc_mcontext.gregs[REG_RAX] == STATUS_INVALID_HANDLE);
}

/* ── Test: NtTerminateProcess with non-exit path (0x2A) ────── */

static void test_nt_terminate_process(void)
{
    printf("\n--- NtTerminateProcess (0x2A, non-exit path) ---\n");

    ucontext_t ctx = create_mock_context();
    /* handle != 0xFFFFFFFF → returns STATUS_SUCCESS without calling exit() */
    ctx.uc_mcontext.gregs[REG_RCX] = 0;
    ctx.uc_mcontext.gregs[REG_RDX] = 0;
    test_syscall_one(0x2A, "NtTerminateProcess", &ctx, STATUS_SUCCESS);
}

/* ── Test: NtGetContextThread (0x24) ───────────────────────── */

static void test_nt_get_context_thread(void)
{
    printf("\n--- NtGetContextThread (0x24) ---\n");

    ucontext_t ctx = create_mock_context();
    ctx.uc_mcontext.gregs[REG_RCX] = 0;  /* thread handle */
    ctx.uc_mcontext.gregs[REG_RDX] = 0;  /* context = NULL */
    test_syscall_one(0x24, "NtGetContextThread", &ctx, STATUS_SUCCESS);
}

/* ── Test: NtSetContextThread (0x26) ───────────────────────── */

static void test_nt_set_context_thread(void)
{
    printf("\n--- NtSetContextThread (0x26) ---\n");

    ucontext_t ctx = create_mock_context();
    ctx.uc_mcontext.gregs[REG_RCX] = 0;  /* thread handle */
    ctx.uc_mcontext.gregs[REG_RDX] = 0;  /* context = NULL */
    test_syscall_one(0x26, "NtSetContextThread", &ctx, STATUS_SUCCESS);
}

/* ── Test: NtAllocateVirtualMemory with NULL base/size (0x18) ─*/

static void test_nt_allocate_virtual_memory(void)
{
    printf("\n--- NtAllocateVirtualMemory (0x18, NULL base/size) ---\n");

    ucontext_t ctx = create_mock_context();

    /* Process handle must be 0xFFFFFFFF for current process */
    ctx.uc_mcontext.gregs[REG_RCX] = 0xFFFFFFFF;
    ctx.uc_mcontext.gregs[REG_RDX] = 0;  /* base_address = NULL */
    ctx.uc_mcontext.gregs[REG_R8]  = 0;  /* zero_bits */
    ctx.uc_mcontext.gregs[REG_R9]  = 0;  /* region_size = NULL */

    /* With NULL base and NULL region_size, mmap(NULL, 0) → MAP_FAILED */
    int rc = handle_syscall(0x18, &ctx);
    check("handle_syscall dispatches NtAllocateVirtualMemory", rc == 0);
    check("NtAllocateVirtualMemory(NULL,NULL) -> STATUS_MEMORY_NOT_AVAILABLE",
          (uint64_t)ctx.uc_mcontext.gregs[REG_RAX] == STATUS_MEMORY_NOT_AVAILABLE);
}

/* ── Test: NtFreeVirtualMemory with NULL base/size (0x19) ──── */

static void test_nt_free_virtual_memory(void)
{
    printf("\n--- NtFreeVirtualMemory (0x19, NULL base/size) ---\n");

    ucontext_t ctx = create_mock_context();

    /* Process handle must be 0xFFFFFFFF for current process */
    ctx.uc_mcontext.gregs[REG_RCX] = 0xFFFFFFFF;
    ctx.uc_mcontext.gregs[REG_RDX] = 0;  /* base_address = NULL */
    ctx.uc_mcontext.gregs[REG_R8]  = 0;  /* region_size = NULL */
    ctx.uc_mcontext.gregs[REG_R9]  = 0;  /* free_type */

    /* base_address == 0 -> handler returns STATUS_INVALID_PARAMETER */
    int rc = handle_syscall(0x19, &ctx);
    check("handle_syscall dispatches NtFreeVirtualMemory", rc == 0);
    check("NtFreeVirtualMemory(NULL) -> STATUS_INVALID_PARAMETER",
          (uint64_t)ctx.uc_mcontext.gregs[REG_RAX] == STATUS_INVALID_PARAMETER);
}

/* ── Test: NtCreateEvent with NULL handle (0x48) ───────────── */

static void test_nt_create_event(void)
{
    printf("\n--- NtCreateEvent (0x48, NULL handle) ---\n");

    ucontext_t ctx = create_mock_context();

    ctx.uc_mcontext.gregs[REG_RCX] = 0;  /* event_handle = NULL */
    ctx.uc_mcontext.gregs[REG_RDX] = 0x10000000;  /* desired_access */
    ctx.uc_mcontext.gregs[REG_R8]  = 0;  /* object_attributes = NULL */
    ctx.uc_mcontext.gregs[REG_R9]  = 0;  /* event_type */
    test_syscall_one(0x48, "NtCreateEvent", &ctx, STATUS_SUCCESS);
}

/* ── Test: argument decoding from registers ─────────────────── */

static void test_argument_decoding(void)
{
    printf("\n--- Argument decoding from registers ---\n");

    ucontext_t ctx = create_mock_context();

    /* Verify that RCX is read as arg1 for NtClose.
     * Use STDIN_HANDLE (0x7FFFFFFF) which returns STATUS_SUCCESS. */
    ctx.uc_mcontext.gregs[REG_RCX] = 0x7FFFFFFFUL;
    ctx.uc_mcontext.gregs[REG_RAX] = 0;

    int rc = handle_syscall(0x0F, &ctx);
    uint64_t rax = (uint64_t)ctx.uc_mcontext.gregs[REG_RAX];
    check("NtClose reads RCX as handle (returns STATUS_SUCCESS)",
          rc == 0 && rax == STATUS_SUCCESS);
}

/* ── Test: multiple syscalls in sequence ────────────────────── */

static void test_sequential_dispatch(void)
{
    printf("\n--- Sequential dispatch (multiple syscalls) ---\n");

    ucontext_t ctx = create_mock_context();

    /* NtCallbackReturn (0x05) -> STATUS_SUCCESS */
    handle_syscall(0x05, &ctx);
    check("1st: NtCallbackReturn -> STATUS_SUCCESS",
          (uint64_t)ctx.uc_mcontext.gregs[REG_RAX] == STATUS_SUCCESS);

    /* NtClose (0x0F) with STDIN_HANDLE -> STATUS_SUCCESS */
    ctx.uc_mcontext.gregs[REG_RCX] = 0x7FFFFFFFUL;
    handle_syscall(0x0F, &ctx);
    check("2nd: NtClose(stdin) -> STATUS_SUCCESS",
          (uint64_t)ctx.uc_mcontext.gregs[REG_RAX] == STATUS_SUCCESS);

    /* NtCallbackReturn (0x05) again -> STATUS_SUCCESS */
    handle_syscall(0x05, &ctx);
    check("3rd: NtCallbackReturn again -> STATUS_SUCCESS",
          (uint64_t)ctx.uc_mcontext.gregs[REG_RAX] == STATUS_SUCCESS);
}

/* ── Main ───────────────────────────────────────────────────── */

int main(void)
{
    install_crash_safety();

    /* Initialize the handle table (constructor in ntdll_handle.c may
     * not run reliably in test binaries; do it explicitly) */
    init_handle_table();

    printf("=== Syscall Dispatch Unit Tests (t7.5) ===\n");

    test_nt_callback_return();
    test_nt_close();
    test_nt_terminate_process();
    test_nt_get_context_thread();
    test_nt_set_context_thread();
    test_nt_allocate_virtual_memory();
    test_nt_free_virtual_memory();
    test_nt_create_event();
    test_argument_decoding();
    test_sequential_dispatch();

    /* ── Summary ──────────────────────────────────────────── */
    printf("\n========================================\n");
    printf("Total:  %d  Passed: %d  Failed: %d\n",
           total_tests, passed_tests, failed_tests);
    printf("========================================\n");

    return failed_tests > 0 ? 1 : 0;
}
