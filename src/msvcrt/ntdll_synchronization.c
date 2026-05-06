/*
 * ntdll_synchronization.c — Synchronization syscall handlers
 *
 * NtSetEvent, NtResetEvent, NtWaitForSingleObject,
 * NtCreateMutex, NtReleaseMutex
 */

#define _GNU_SOURCE
#include <stdint.h>
#include <pthread.h>
#include <time.h>
#include "handler_abi.h"
#include "ntdll_priv.h"
#include "../syscall/syscalls_inline.h"

/* ── Globals ───────────────────────────────────────────────────── */

pthread_mutex_t events_global_mutex = PTHREAD_MUTEX_INITIALIZER;

wine_mutex_t mutexes[MAX_MUTEXES];
int mutex_count = 0;

/* ── Helper: find event by handle ──────────────────────────────── */

static int find_event(int handle)
{
    for (int i = 0; i < event_count; i++) {
        if (events[i].handle == handle)
            return i;
    }
    return -1;
}

/* ── Helper: find mutex by handle ──────────────────────────────── */

static int find_mutex(int handle)
{
    for (int i = 0; i < mutex_count; i++) {
        if (mutexes[i].handle == handle)
            return i;
    }
    return -1;
}

/* ── NtSetEvent (0x5C) ───────────────────────────────────────────
 *
 * Set an event to the signaled state and broadcast any waiting threads.
 * arg1: handle
 * arg2: previous_state (pointer to write previous signaled state, optional)
 */
HANDLER
uint64_t handler_NtSetEvent(uint64_t handle, uint64_t previous_state)
{
    int slot = find_event((int)handle);
    if (slot < 0)
        return STATUS_INVALID_HANDLE;

    if (previous_state != 0)
        *(uint64_t *)(uintptr_t)previous_state = events[slot].signaled;

    events[slot].signaled = 1;
    pthread_cond_broadcast(&events[slot].cond);
    return STATUS_SUCCESS;
}

/* ── NtResetEvent (0x5E) ─────────────────────────────────────────
 *
 * Clear a manual-reset event. (Auto-reset is handled in WaitForSingleObject.)
 * arg1: handle
 * arg2: previous_state (pointer to write previous signaled state, optional)
 */
HANDLER
uint64_t handler_NtResetEvent(uint64_t handle, uint64_t previous_state)
{
    int slot = find_event((int)handle);
    if (slot < 0)
        return STATUS_INVALID_HANDLE;

    if (previous_state != 0)
        *(uint64_t *)(uintptr_t)previous_state = events[slot].signaled;

    events[slot].signaled = 0;
    return STATUS_SUCCESS;
}

/* ── NtWaitForSingleObject (0x03) ────────────────────────────────
 *
 * Wait for an event to be signaled, with optional timeout.
 * arg1: handle
 * arg2: alertable (BOOL, ignored)
 * arg3: timeout_ptr (PVOID to LARGE_INTEGER; NULL = forever, negative = relative)
 *
 * For simplicity: only checks events. Mutexes are not waitable here.
 * Uses spin-sleep loop: lock global mutex, check signaled, if not
 * signaled with timeout, unlock and nanosleep, repeat.
 */
HANDLER
uint64_t handler_NtWaitForSingleObject(uint64_t handle, uint64_t alertable, uint64_t timeout_ptr)
{
    (void)alertable;
    int slot = find_event((int)handle);
    if (slot < 0)
        return STATUS_INVALID_HANDLE;

    /* No timeout → spin until signaled */
    if (timeout_ptr == 0) {
        /* Spin with short sleeps until signaled */
        while (1) {
            if (events[slot].signaled)
                break;
            struct timespec ts = {0, 1000000}; /* 1ms */
            INLINE_SYSCALL_NANOSLEEP(&ts, NULL);
        }
        /* Auto-reset: if event_type == 1 (Synchronization), reset it */
        if (events[slot].event_type == 1)
            events[slot].signaled = 0;
        return STATUS_SUCCESS;
    }

    /* Read timeout value */
    int64_t timeout_100ns = *(int64_t *)(uintptr_t)timeout_ptr;

    /* Absolute timeout == 0 → return immediately */
    if (timeout_100ns == 0) {
        if (events[slot].signaled) {
            if (events[slot].event_type == 1)
                events[slot].signaled = 0;
            return STATUS_SUCCESS;
        }
        return 0x00000080UL; /* STATUS_TIMEOUT */
    }

    /* Relative delay: negative value */
    if (timeout_100ns < 0) {
        uint64_t delay_100ns = (uint64_t)(-timeout_100ns);
        uint64_t delay_ns = delay_100ns * 100;
        uint64_t elapsed = 0;

        while (elapsed < delay_ns) {
            if (events[slot].signaled) {
                if (events[slot].event_type == 1)
                    events[slot].signaled = 0;
                return STATUS_SUCCESS;
            }
            uint64_t remaining = delay_ns - elapsed;
            uint64_t sleep_us = (remaining / 1000);
            if (sleep_us > 100000) sleep_us = 100000; /* max 100ms per iteration */
            struct timespec ts = {(long)(sleep_us / 1000000), (long)((sleep_us % 1000000) * 1000)};
            struct timespec rem = {0};
            long r = INLINE_SYSCALL_NANOSLEEP(&ts, &rem);
            uint64_t slept = sleep_us * 1000; /* to ns */
            if (r != 0) slept = (uint64_t)rem.tv_sec * 1000000000UL + (uint64_t)rem.tv_nsec;
            elapsed += slept;
        }
    }

    /* Absolute timeout: positive value — simplified: return timeout */
    /* (Real implementation would compare against current time) */

    if (events[slot].signaled) {
        if (events[slot].event_type == 1)
            events[slot].signaled = 0;
        return STATUS_SUCCESS;
    }
    return 0x00000080UL; /* STATUS_TIMEOUT */
}

/* ── NtCreateMutex (0x44) ────────────────────────────────────────
 *
 * Create a mutex object.
 * arg1: mutex_handle (pointer to write handle)
 * arg2: desired_access (ignored)
 * arg3: object_attributes (ignored)
 */
HANDLER
uint64_t handler_NtCreateMutex(uint64_t *mutex_handle, uint64_t desired_access,
                                uint64_t object_attributes)
{
    (void)desired_access;
    (void)object_attributes;

    if (mutex_count >= MAX_MUTEXES)
        return STATUS_MEMORY_NOT_AVAILABLE;

    /* Find a free slot in the handle table */
    uint64_t handle = 0;
    {
        unsigned idx;
        for (idx = 3; idx < HANDLE_TABLE_SIZE; idx++) {
            if (!handle_table[idx].used) {
                handle_table[idx].fd   = -1;
                handle_table[idx].used = 1;
                handle = (uint64_t)idx;
                break;
            }
        }
    }

    if (handle == 0)
        return STATUS_MEMORY_NOT_AVAILABLE;

    int slot = mutex_count++;
    mutexes[slot].handle = (int)handle;
    mutexes[slot].locked = 0;
    pthread_mutex_init(&mutexes[slot].mutex, NULL);

    if (mutex_handle != NULL)
        *mutex_handle = handle;

    return STATUS_SUCCESS;
}

/* ── NtReleaseMutex (0x1E) ───────────────────────────────────────
 *
 * Release a mutex.
 * arg1: handle
 * arg2: alertable (ignored)
 */
HANDLER
uint64_t handler_NtReleaseMutex(uint64_t handle, uint64_t alertable)
{
    (void)alertable;
    int slot = find_mutex((int)handle);
    if (slot < 0)
        return STATUS_INVALID_HANDLE;

    if (!mutexes[slot].locked)
        return STATUS_INVALID_HANDLE; /* not locked by anyone */

    mutexes[slot].locked = 0;
    pthread_mutex_unlock(&mutexes[slot].mutex);
    return STATUS_SUCCESS;
}
