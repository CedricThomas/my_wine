#define _GNU_SOURCE
#include <stdint.h>
#include <stdlib.h>
#include <time.h>
#include "handler_abi.h"
#include "ntdll_priv.h"
#include "../syscalls_inline.h"

/*
 * ntdll_time.c — Time-related NT syscall handlers
 *
 * NtQuerySystemTime, NtQueryPerformanceCounter, NtQueryPerformanceFrequency,
 * NtDelayExecution.
 */

/* ── NtQuerySystemTime (0x09) ──────────────────────────────────────
 *
 * Returns the current system time as a FILETIME (64-bit: 100ns since 1601-01-01).
 * FILETIME is written to the pointer in arg1.
 *
 * Windows epoch: 1601-01-01 UTC → Unix epoch 1970-01-01 UTC = 11644473600 seconds
 *                = 116444736000000000 100ns units.
 */
HANDLER
uint64_t handler_NtQuerySystemTime(uint64_t ft_ptr)
{
    struct timespec ts;
    if (INLINE_SYSCALL_CLOCK_GETTIME(CLOCK_REALTIME, &ts) != 0) {
        return STATUS_UNSUCCESSFUL;
    }
    uint64_t filetime = (uint64_t)ts.tv_sec * 10000000ULL + (uint64_t)ts.tv_nsec / 100ULL;
    filetime += 116444736000000000ULL; /* 1601→1970 epoch offset in 100ns */
    if (ft_ptr != 0) {
        *(uint64_t *)(uintptr_t)ft_ptr = filetime;
    }
    return STATUS_SUCCESS;
}

/* ── NtQueryPerformanceCounter (0x55) ─────────────────────────────
 *
 * Returns a high-resolution performance counter as 100ns ticks
 * since some arbitrary point (monotonic clock).
 */
HANDLER
uint64_t handler_NtQueryPerformanceCounter(uint64_t li_ptr)
{
    struct timespec ts;
    if (INLINE_SYSCALL_CLOCK_GETTIME(CLOCK_MONOTONIC, &ts) != 0) {
        return STATUS_UNSUCCESSFUL;
    }
    uint64_t counter = (uint64_t)ts.tv_sec * 10000000ULL + (uint64_t)ts.tv_nsec / 100ULL;
    if (li_ptr != 0) {
        *(uint64_t *)(uintptr_t)li_ptr = counter;
    }
    return STATUS_SUCCESS;
}

/* ── NtQueryPerformanceFrequency (0x56) ───────────────────────────
 *
 * Returns the frequency of the performance counter (10^7 = 100ns resolution).
 */
HANDLER
uint64_t handler_NtQueryPerformanceFrequency(uint64_t li_ptr)
{
    if (li_ptr != 0) {
        *(uint64_t *)(uintptr_t)li_ptr = 10000000ULL; /* 10^7 = 100ns resolution */
    }
    return STATUS_SUCCESS;
}

/* ── NtDelayExecution (0x1A) ─────────────────────────────────────
 *
 * arg1: alarm_pending (BOOL, ignored - we just sleep)
 * arg2: timeout_ptr (PVOID to LARGE_INTEGER)
 *   NULL = sleep forever (not implemented here, just return)
 *   High bit set = relative delay
 *   Value is in 100ns units; convert to nanoseconds (val * 100).
 */
HANDLER
uint64_t handler_NtDelayExecution(uint64_t alarm_pending, uint64_t timeout_ptr)
{
    (void)alarm_pending;
    if (timeout_ptr == 0) {
        /* NULL timeout = infinite delay. Return STATUS_SUCCESS.
         * In real NT this blocks forever; here we just return. */
        return STATUS_SUCCESS;
    }
    
    int64_t timeout_100ns = *(int64_t *)(uintptr_t)timeout_ptr;
    
    if (timeout_100ns >= 0) {
        /* Absolute timeout — not implemented for unit tests, return SUCCESS */
        return STATUS_SUCCESS;
    }
    
    /* Relative delay: negate the value */
    uint64_t delay_100ns = (uint64_t)(-timeout_100ns);
    struct timespec ts;
    ts.tv_sec = (delay_100ns / 10000000ULL);
    ts.tv_nsec = (delay_100ns % 10000000ULL) * 100;
    
    struct timespec rem = {0};
    long ret = INLINE_SYSCALL_NANOSLEEP(&ts, &rem);
    if (ret != 0) {
        return STATUS_UNSUCCESSFUL;
    }
    return STATUS_SUCCESS;
}
