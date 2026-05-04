/*
 * ntdll_process.c — Process-level syscall handlers
 *
 * NtTerminateProcess, NtCallbackReturn, NtQueryInformationProcess
 */

#include <stdint.h>
#include <stddef.h>
#include <asm/unistd_64.h>
#include "handler_abi.h"
#include "ntdll_priv.h"

HANDLER
uint64_t handler_NtTerminateProcess(uint64_t process_handle, uint64_t exit_status)
{
    if (process_handle != 0xFFFFFFFF)
        return STATUS_SUCCESS;

    long ret;
    __asm__ volatile("syscall" : "=a"(ret) : "a"(__NR_exit), "D"((unsigned long)exit_status) : "rcx", "r11", "cc");
    /* __builtin_unreachable() ensures the compiler knows this doesn't return */
    __builtin_unreachable();
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
        long ret;
        __asm__ volatile("syscall" : "=a"(ret) : "a"(__NR_getpid) : "rcx", "r11", "cc");
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
