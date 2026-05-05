/*
 * test_syscall_dispatch.c — Syscall dispatch unit tests
 *
 * Populates __wine_guest_regs manually and calls c_dispatch_syscall()
 * directly (single-process dispatch model). Verifies that the correct
 * handler is invoked and the return value is set in RAX.
 *
 * Tests safe syscalls that do not call exit() or dereference
 * guest pointers that would segfault.
 *
 * Build: linked against dispatcher.o and all
 *   ntdll handler .o files.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <signal.h>

#include "ntdll.h"
#include "syscall/dispatcher_entry.h"
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

/*
 * Test a syscall and verify the result.
 */
static void test_syscall_one(uint64_t nr, const char *label, uint64_t expected_rax)
{
    printf("  Testing %s (0x%lx)...\n", label, (unsigned long)nr);
    uint64_t result = c_dispatch_syscall(nr);
    check("c_dispatch_syscall returns expected value", result == expected_rax);
    check("RAX set to expected value", __wine_guest_regs.rax == expected_rax);
}

/* ── Test: NtCallbackReturn (0x05) ─────────────────────────── */

static void test_nt_callback_return(void)
{
    printf("\n--- NtCallbackReturn (0x05) ---\n");

    /* NtCallbackReturn takes no arguments; registers don't matter */
    __wine_guest_regs.rcx = 0;
    __wine_guest_regs.rdx = 0;
    __wine_guest_regs.r8 = 0;
    __wine_guest_regs.r9 = 0;
    __wine_guest_regs.rsp = 0;
    test_syscall_one(0x05, "NtCallbackReturn", STATUS_SUCCESS);
}

/* ── Test: NtClose (0x0F) ──────────────────────────────────── */

static void test_nt_close(void)
{
    printf("\n--- NtClose (0x0F) ---\n");

    /* Test with valid handle: STD_INPUT_HANDLE_VALUE = 0x7FFFFFFF
     * (special-cased in handler — not actually closed) */
    __wine_guest_regs.rcx = 0x7FFFFFFFUL;  /* STD_INPUT_HANDLE_VALUE */
    __wine_guest_regs.rdx = 0;
    __wine_guest_regs.r8 = 0;
    __wine_guest_regs.r9 = 0;
    __wine_guest_regs.rsp = 0;
    test_syscall_one(0x0F, "NtClose (stdin)", STATUS_SUCCESS);

    /* Test with invalid handle */
    __wine_guest_regs.rcx = 0xDEAD;  /* not in handle table */
    __wine_guest_regs.rdx = 0;
    __wine_guest_regs.r8 = 0;
    __wine_guest_regs.r9 = 0;
    __wine_guest_regs.rsp = 0;
    uint64_t result = c_dispatch_syscall(0x0F);
    check("NtClose(invalid) returns STATUS_INVALID_HANDLE",
          result == STATUS_INVALID_HANDLE);
}

/* ── Test: NtTerminateProcess with non-exit path (0x2A) ────── */

static void test_nt_terminate_process(void)
{
    printf("\n--- NtTerminateProcess (0x2A, non-exit path) ---\n");

    /* handle != 0xFFFFFFFF → returns STATUS_SUCCESS without calling exit() */
    __wine_guest_regs.rcx = 0;
    __wine_guest_regs.rdx = 0;
    __wine_guest_regs.r8 = 0;
    __wine_guest_regs.r9 = 0;
    __wine_guest_regs.rsp = 0;
    test_syscall_one(0x2A, "NtTerminateProcess", STATUS_SUCCESS);
}

/* ── Test: NtGetContextThread (0x24) ───────────────────────── */

static void test_nt_get_context_thread(void)
{
    printf("\n--- NtGetContextThread (0x24) ---\n");

    __wine_guest_regs.rcx = 0;  /* thread handle */
    __wine_guest_regs.rdx = 0;  /* context = NULL */
    __wine_guest_regs.r8 = 0;
    __wine_guest_regs.r9 = 0;
    __wine_guest_regs.rsp = 0;
    test_syscall_one(0x24, "NtGetContextThread", STATUS_SUCCESS);
}

/* ── Test: NtSetContextThread (0x26) ───────────────────────── */

static void test_nt_set_context_thread(void)
{
    printf("\n--- NtSetContextThread (0x26) ---\n");

    __wine_guest_regs.rcx = 0;  /* thread handle */
    __wine_guest_regs.rdx = 0;  /* context = NULL */
    __wine_guest_regs.r8 = 0;
    __wine_guest_regs.r9 = 0;
    __wine_guest_regs.rsp = 0;
    test_syscall_one(0x26, "NtSetContextThread", STATUS_SUCCESS);
}

/* ── Test: NtAllocateVirtualMemory with NULL base/size (0x18) ─*/

static void test_nt_allocate_virtual_memory(void)
{
    printf("\n--- NtAllocateVirtualMemory (0x18, NULL base/size) ---\n");

    /* Process handle must be 0xFFFFFFFF for current process */
    __wine_guest_regs.rcx = 0xFFFFFFFF;
    __wine_guest_regs.rdx = 0;  /* base_address = NULL */
    __wine_guest_regs.r8 = 0;   /* zero_bits */
    __wine_guest_regs.r9 = 0;   /* region_size = NULL */
    __wine_guest_regs.rsp = 0;

    /* With NULL base and NULL region_size, mmap(NULL, 0) → MAP_FAILED */
    uint64_t result = c_dispatch_syscall(0x18);
    check("c_dispatch_syscall dispatches NtAllocateVirtualMemory", result != (uint64_t)-1);
    check("NtAllocateVirtualMemory(NULL,NULL) -> STATUS_MEMORY_NOT_AVAILABLE",
          __wine_guest_regs.rax == STATUS_MEMORY_NOT_AVAILABLE);
}

/* ── Test: NtFreeVirtualMemory with NULL base/size (0x19) ──── */

static void test_nt_free_virtual_memory(void)
{
    printf("\n--- NtFreeVirtualMemory (0x19, NULL base/size) ---\n");

    /* Process handle must be 0xFFFFFFFF for current process */
    __wine_guest_regs.rcx = 0xFFFFFFFF;
    __wine_guest_regs.rdx = 0;  /* base_address = NULL */
    __wine_guest_regs.r8 = 0;   /* region_size = NULL */
    __wine_guest_regs.r9 = 0;   /* free_type */
    __wine_guest_regs.rsp = 0;

    /* base_address == 0 -> handler returns STATUS_INVALID_PARAMETER */
    uint64_t result = c_dispatch_syscall(0x19);
    check("c_dispatch_syscall dispatches NtFreeVirtualMemory", result != (uint64_t)-1);
    check("NtFreeVirtualMemory(NULL) -> STATUS_INVALID_PARAMETER",
          __wine_guest_regs.rax == STATUS_INVALID_PARAMETER);
}

/* ── Test: NtCreateEvent with NULL handle (0x48) ───────────── */

static void test_nt_create_event(void)
{
    printf("\n--- NtCreateEvent (0x48, NULL handle) ---\n");

    __wine_guest_regs.rcx = 0;  /* event_handle = NULL */
    __wine_guest_regs.rdx = 0x10000000;  /* desired_access */
    __wine_guest_regs.r8 = 0;  /* object_attributes = NULL */
    __wine_guest_regs.r9 = 0;  /* event_type */
    __wine_guest_regs.rsp = 0;
    test_syscall_one(0x48, "NtCreateEvent", STATUS_SUCCESS);
}

/* ── Test: argument decoding from registers ─────────────────── */

static void test_argument_decoding(void)
{
    printf("\n--- Argument decoding from registers ---\n");

    /* Verify that RCX is read as arg1 for NtClose.
     * Use STD_INPUT_HANDLE_VALUE (0x7FFFFFFF) which returns STATUS_SUCCESS. */
    __wine_guest_regs.rcx = 0x7FFFFFFFUL;
    __wine_guest_regs.rdx = 0;
    __wine_guest_regs.r8 = 0;
    __wine_guest_regs.r9 = 0;
    __wine_guest_regs.rsp = 0;

    uint64_t result = c_dispatch_syscall(0x0F);
    check("NtClose reads RCX as handle (returns STATUS_SUCCESS)",
          result == STATUS_SUCCESS);
    check("RAX set to STATUS_SUCCESS", __wine_guest_regs.rax == STATUS_SUCCESS);
}

/* ── Test: multiple syscalls in sequence ────────────────────── */

static void test_sequential_dispatch(void)
{
    printf("\n--- Sequential dispatch (multiple syscalls) ---\n");

    /* NtCallbackReturn (0x05) -> STATUS_SUCCESS */
    __wine_guest_regs.rcx = 0;
    __wine_guest_regs.rdx = 0;
    __wine_guest_regs.r8 = 0;
    __wine_guest_regs.r9 = 0;
    __wine_guest_regs.rsp = 0;
    c_dispatch_syscall(0x05);
    check("1st: NtCallbackReturn -> STATUS_SUCCESS",
          __wine_guest_regs.rax == STATUS_SUCCESS);

    /* NtClose (0x0F) with STD_INPUT_HANDLE_VALUE -> STATUS_SUCCESS */
    __wine_guest_regs.rcx = 0x7FFFFFFFUL;
    __wine_guest_regs.rdx = 0;
    __wine_guest_regs.r8 = 0;
    __wine_guest_regs.r9 = 0;
    __wine_guest_regs.rsp = 0;
    c_dispatch_syscall(0x0F);
    check("2nd: NtClose(stdin) -> STATUS_SUCCESS",
          __wine_guest_regs.rax == STATUS_SUCCESS);

    /* NtCallbackReturn (0x05) again -> STATUS_SUCCESS */
    __wine_guest_regs.rcx = 0;
    __wine_guest_regs.rdx = 0;
    __wine_guest_regs.r8 = 0;
    __wine_guest_regs.r9 = 0;
    __wine_guest_regs.rsp = 0;
    c_dispatch_syscall(0x05);
    check("3rd: NtCallbackReturn again -> STATUS_SUCCESS",
          __wine_guest_regs.rax == STATUS_SUCCESS);
}

/* ── Test: NtQuerySystemTime (0x09) ─────────────────────────── */

static void test_nt_query_system_time(void)
{
    printf("\n--- NtQuerySystemTime (0x09) ---\n");
    uint64_t filetime_val = 0;
    __wine_guest_regs.rcx = (uint64_t)&filetime_val;
    __wine_guest_regs.rdx = 0;
    __wine_guest_regs.r8 = 0;
    __wine_guest_regs.r9 = 0;
    __wine_guest_regs.rsp = 0;
    test_syscall_one(0x09, "NtQuerySystemTime", STATUS_SUCCESS);
    check("FILETIME value written and non-zero", filetime_val > 0);

    /* NULL ptr returns STATUS_SUCCESS */
    __wine_guest_regs.rcx = 0;
    test_syscall_one(0x09, "NtQuerySystemTime(NULL)", STATUS_SUCCESS);
}

/* ── Test: NtQueryPerformanceCounter (0x55) ─────────────────── */

static void test_nt_query_performance_counter(void)
{
    printf("\n--- NtQueryPerformanceCounter (0x55) ---\n");
    uint64_t counter_val = 0;
    __wine_guest_regs.rcx = (uint64_t)&counter_val;
    __wine_guest_regs.rdx = 0;
    __wine_guest_regs.r8 = 0;
    __wine_guest_regs.r9 = 0;
    __wine_guest_regs.rsp = 0;
    test_syscall_one(0x55, "NtQueryPerformanceCounter", STATUS_SUCCESS);
    check("Counter value written and non-zero", counter_val > 0);

    /* NULL ptr returns STATUS_SUCCESS */
    __wine_guest_regs.rcx = 0;
    test_syscall_one(0x55, "NtQueryPerformanceCounter(NULL)", STATUS_SUCCESS);
}

/* ── Test: NtQueryPerformanceFrequency (0x56) ───────────────── */

static void test_nt_query_performance_frequency(void)
{
    printf("\n--- NtQueryPerformanceFrequency (0x56) ---\n");
    uint64_t freq_val = 0;
    __wine_guest_regs.rcx = (uint64_t)&freq_val;
    __wine_guest_regs.rdx = 0;
    __wine_guest_regs.r8 = 0;
    __wine_guest_regs.r9 = 0;
    __wine_guest_regs.rsp = 0;
    test_syscall_one(0x56, "NtQueryPerformanceFrequency", STATUS_SUCCESS);
    check("Frequency is 10^7 (10000000)", freq_val == 10000000ULL);

    /* NULL ptr returns STATUS_SUCCESS */
    __wine_guest_regs.rcx = 0;
    test_syscall_one(0x56, "NtQueryPerformanceFrequency(NULL)", STATUS_SUCCESS);
}

/* ── Test: NtDelayExecution (0x1A) ─────────────────────────── */

static void test_nt_delay_execution(void)
{
    printf("\n--- NtDelayExecution (0x1A) ---\n");

    /* NULL timeout returns STATUS_SUCCESS (infinite delay → immediate return) */
    __wine_guest_regs.rcx = 0;  /* alarm_pending = FALSE */
    __wine_guest_regs.rdx = 0;  /* timeout_ptr = NULL */
    __wine_guest_regs.r8 = 0;
    __wine_guest_regs.r9 = 0;
    __wine_guest_regs.rsp = 0;
    test_syscall_one(0x1A, "NtDelayExecution(NULL)", STATUS_SUCCESS);

    /* Zero timeout (immediate return) */
    int64_t timeout_val = 0;  /* absolute 0 → return SUCCESS */
    __wine_guest_regs.rcx = 0;
    __wine_guest_regs.rdx = (uint64_t)&timeout_val;
    test_syscall_one(0x1A, "NtDelayExecution(0 timeout)", STATUS_SUCCESS);

    /* Very small relative delay */
    timeout_val = -100000; /* -100000 * 100ns = 10ms */
    __wine_guest_regs.rcx = 0;
    __wine_guest_regs.rdx = (uint64_t)&timeout_val;
    test_syscall_one(0x1A, "NtDelayExecution(10ms relative)", STATUS_SUCCESS);
}

/* ── Test: Unhandled syscalls return STATUS_NOT_IMPLEMENTED ─── */

static void test_unhandled_syscalls(void)
{
    printf("\n--- Unhandled syscalls (STATUS_NOT_IMPLEMENTED) ---\n");

    __wine_guest_regs.rcx = 0;
    __wine_guest_regs.rdx = 0;
    __wine_guest_regs.r8 = 0;
    __wine_guest_regs.r9 = 0;
    __wine_guest_regs.rsp = 0;

    /* 0xFF is not a registered syscall number */
    uint64_t result = c_dispatch_syscall(0xFF);
    check("Unhandled syscall 0xFF → STATUS_NOT_IMPLEMENTED",
          result == STATUS_NOT_IMPLEMENTED);
    check("RAX set to STATUS_NOT_IMPLEMENTED",
          __wine_guest_regs.rax == STATUS_NOT_IMPLEMENTED);

    /* 0x99 is not a registered syscall number */
    result = c_dispatch_syscall(0x99);
    check("Unhandled syscall 0x99 → STATUS_NOT_IMPLEMENTED",
          result == STATUS_NOT_IMPLEMENTED);

    /* 0x3F is between existing syscall numbers but unhandled */
    result = c_dispatch_syscall(0x3F);
    check("Unhandled syscall 0x3F → STATUS_NOT_IMPLEMENTED",
          result == STATUS_NOT_IMPLEMENTED);
}

/* ── Main ───────────────────────────────────────────────────── */

int main(void)
{
    install_crash_safety();

    /* Initialize the handle table (constructor in ntdll_handle.c may
     * not run reliably in test binaries; do it explicitly) */
    init_handle_table();

    printf("=== Syscall Dispatch Unit Tests ===\n");

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
    test_nt_query_system_time();
    test_nt_query_performance_counter();
    test_nt_query_performance_frequency();
    test_nt_delay_execution();
    test_unhandled_syscalls();

    /* ── Summary ──────────────────────────────────────────── */
    printf("\n========================================\n");
    printf("Total:  %d  Passed: %d  Failed: %d\n",
           total_tests, passed_tests, failed_tests);
    printf("========================================\n");

    return failed_tests > 0 ? 1 : 0;
}
