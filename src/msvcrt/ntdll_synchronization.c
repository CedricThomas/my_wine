/*
 * ntdll_synchronization.c — Synchronization syscall handlers
 *
 * NtSetEvent, NtResetEvent, NtWaitForSingleObject,
 * NtCreateMutex, NtReleaseMutex
 */

#define _GNU_SOURCE
#include <stdint.h>
#include <time.h>
#include "handler_abi.h"
#include "ntdll_priv.h"
#include "../syscall/syscalls_inline.h"

/* ── Helper: find event by handle ──────────────────────────────── */

static int find_event(int handle)
{
    for (int i = 0; i < ko_event_count(); i++) {
        if (ko_event(i)->handle == handle)
            return i;
    }
    return -1;
}

/* ── Helper: find mutex by handle ──────────────────────────────── */

static int find_mutex(int handle)
{
    for (int i = 0; i < ko_mutex_count(); i++) {
        if (ko_mutex(i)->handle == handle)
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
        *(uint64_t *)(uintptr_t)previous_state = ko_event(slot)->signaled;

    ko_event(slot)->signaled = 1;
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
        *(uint64_t *)(uintptr_t)previous_state = ko_event(slot)->signaled;

    ko_event(slot)->signaled = 0;
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
            if (ko_event(slot)->signaled)
                break;
            struct timespec ts = {0, 1000000}; /* 1ms */
            INLINE_SYSCALL_NANOSLEEP(&ts, NULL);
        }
        /* Auto-reset: if event_type == 1 (Synchronization), reset it */
        if (ko_event(slot)->event_type == 1)
            ko_event(slot)->signaled = 0;
        return STATUS_SUCCESS;
    }

    /* Read timeout value */
    int64_t timeout_100ns = *(int64_t *)(uintptr_t)timeout_ptr;

    /* Absolute timeout == 0 → return immediately */
    if (timeout_100ns == 0) {
        if (ko_event(slot)->signaled) {
            if (ko_event(slot)->event_type == 1)
                ko_event(slot)->signaled = 0;
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
            if (ko_event(slot)->signaled) {
                if (ko_event(slot)->event_type == 1)
                    ko_event(slot)->signaled = 0;
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

    if (ko_event(slot)->signaled) {
        if (ko_event(slot)->event_type == 1)
            ko_event(slot)->signaled = 0;
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

    if (ko_mutex_count() >= MAX_MUTEXES)
        return STATUS_MEMORY_NOT_AVAILABLE;

    int slot = ko_mutex_count();
    ko_set_mutex_count(slot + 1);
    uint32_t handle = wine_handle_alloc(HANDLE_TYPE_MUTEX, (void *)ko_mutex(slot));

    if (handle == 0) {
        ko_set_mutex_count(slot);
        return STATUS_MEMORY_NOT_AVAILABLE;
    }

    ko_mutex(slot)->handle = (int)handle;
    ko_mutex(slot)->locked = 0;

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

    if (!ko_mutex(slot)->locked)
        return STATUS_INVALID_HANDLE; /* not locked by anyone */

    ko_mutex(slot)->locked = 0;
    return STATUS_SUCCESS;
}

/* ── NtCreateSemaphore (0x4C) ─────────────────────────────────────
 *
 * Create a semaphore object.
 * arg1: semaphore_handle (pointer to write handle)
 * arg2: desired_access (ignored)
 * arg3: object_attributes (ignored)
 * arg4: initial_count
 * arg5: maximum_count
 */
HANDLER
uint64_t handler_NtCreateSemaphore(uint64_t *semaphore_handle, uint64_t desired_access,
                                    uint64_t object_attributes, uint64_t initial_count,
                                    uint64_t maximum_count)
{
    (void)desired_access;
    (void)object_attributes;

    if (maximum_count == 0)
        return STATUS_INVALID_PARAMETER;

    if (ko_semaphore_count() >= MAX_SEMAPHORES)
        return STATUS_MEMORY_NOT_AVAILABLE;

    if (initial_count > maximum_count)
        initial_count = maximum_count;

    int slot = ko_semaphore_count();
    ko_set_semaphore_count(slot + 1);
    uint32_t handle = wine_handle_alloc(HANDLE_TYPE_SEMAPHORE, (void *)ko_semaphore(slot));

    if (handle == 0) {
        ko_set_semaphore_count(slot);
        return STATUS_MEMORY_NOT_AVAILABLE;
    }

    ko_semaphore(slot)->handle = (int)handle;
    ko_semaphore(slot)->count = (int)initial_count;
    ko_semaphore(slot)->max_count = (int)maximum_count;

    if (semaphore_handle != NULL)
        *semaphore_handle = handle;

    return STATUS_SUCCESS;
}
