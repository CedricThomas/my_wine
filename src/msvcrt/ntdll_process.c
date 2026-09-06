/*
 * ntdll_process.c — Process-level syscall handlers
 *
 * NtTerminateProcess, NtCallbackReturn, NtQueryInformationProcess
 */

#include <stdint.h>
#include <stddef.h>
#include "handler_abi.h"
#include "ntdll_priv.h"
#include "../syscall/syscalls_inline.h"

/* Cleanup functions — no longer called in handler_NtTerminateProcess.
 * INLINE_SYSCALL_EXIT terminates the process immediately, so cleanup
 * is deferred to the OS. These externs are kept for potential future use.
 * __attribute__((weak)) so test builds (which don't link loader/syscall modules)
 * don't get undefined reference errors. */
__attribute__((weak)) extern void cleanup_unix_stack(void);
__attribute__((weak)) extern void cleanup_thunk_pages(void);
__attribute__((weak)) extern void *get_gs_base(void);
__attribute__((weak)) extern void cleanup_guest(void *teb, void *stack_base);
__attribute__((weak)) extern void *g_stack_base;

HANDLER
uint64_t handler_NtTerminateProcess(uint64_t process_handle, uint64_t exit_status)
{
    if (process_handle != 0xFFFFFFFF)
        return STATUS_SUCCESS;

    /* Exit the entire process, not just the current thread.
     * SDL and the host runtime may have spawned helper threads by the time a
     * window-close path reaches ExitProcess. Using sys_exit can leave those
     * threads alive after the guest main thread terminates, which matches the
     * observed "window gone but process still running" hang.
     *
     * We still cannot call cleanup_unix_stack() or cleanup_thunk_pages() here:
     * cleanup_unix_stack() unmaps the UNIX stack we're currently executing on,
     * and cleanup_thunk_pages() unmaps the thunk code the dispatcher returns to.
     * All cleanup is deferred to the OS when exit_group tears down the process.
     */
    INLINE_SYSCALL_EXIT_GROUP((unsigned long)exit_status);
}

HANDLER
uint64_t handler_NtCallbackReturn(void)
{
    return STATUS_SUCCESS;
}

/*
 * handler_NtQueryInformationProcess
 *
 * PROCESS_BASIC_INFORMATION (x64, 40 bytes):
 *   uint64_t  ExitStatus
 *   uint64_t  PebBaseAddress
 *   uint64_t  AffinityMask
 *   int32_t   BasePriority
 *   uint8_t   pad[4]
 *   uint64_t  UniqueProcessId
 *   uint64_t  InheritedFromUniqueProcessId
 *
 * VM_COUNTERS (x64, 80 bytes) for ProcessWorkingSetSize:
 *   12 uint64_t fields
 */
HANDLER
uint64_t handler_NtQueryInformationProcess(uint64_t process_handle,
                                            uint64_t info_class,
                                            uint64_t buffer,
                                            uint64_t length,
                                            uint64_t return_length)
{
    (void)process_handle; /* we only know about ourselves */

    switch ((int)info_class) {
    case 0: { /* ProcessBasicInformation — 40 bytes */
        if (length < 40) return STATUS_BUFFER_TOO_SMALL;
        /*
         * Layout (40 bytes): ExitStatus(8) PebBaseAddr(8) Affinity(8)
         *                    BasePriority(8) PID(8) InheritedPID(8)
         */
        uint64_t *out = (uint64_t *)(uintptr_t)buffer;
        out[0] = 0;                         /* ExitStatus */
        out[1] = 0;                         /* PebBaseAddress — set by loader */
        out[2] = 1;                         /* AffinityMask */
        out[3] = 8;                         /* BasePriority */
        long ret = INLINE_SYSCALL_GETPID();
        out[4] = (uint64_t)ret;             /* UniqueProcessId */
        out[5] = (uint64_t)ret;             /* InheritedFromUniqueProcessId */
        if (return_length) *(uint32_t *)(uintptr_t)return_length = 40;
        return STATUS_SUCCESS;
    }
    case 10: { /* ProcessWorkingSetSize — VM_COUNTERS, 80 bytes */
        if (length < 80) return STATUS_BUFFER_TOO_SMALL;
        uint64_t *out = (uint64_t *)(uintptr_t)buffer;
        /* All zeros is a valid approximation */
        __builtin_memset(out, 0, 80);
        if (return_length) *(uint32_t *)(uintptr_t)return_length = 80;
        return STATUS_SUCCESS;
    }
    default:
        return STATUS_UNSUCCESSFUL;
    }
}
