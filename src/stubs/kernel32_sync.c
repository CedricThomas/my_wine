/*
 * kernel32_sync.c — Synchronization kernel32 stubs
 *
 * CreateEventA, SetEvent, ResetEvent, WaitForSingleObject,
 * CreateMutexA, ReleaseMutex
 *
 * These map to the corresponding Nt* syscall handlers.
 */

#define _GNU_SOURCE
#include "kernel32_priv.h"
#include <pthread.h>

/* ── CreateEventA ────────────────────────────────────────────── */
/*
 * Create an event object. Maps to NtCreateEvent.
 * lpAttributes, lpName ignored → NULL. ManualReset=event_type, initialState.
 */
WINE_STUB
void *CreateEventA(void *lpAttributes, int bManualReset, int bInitialState, const char *lpName)
{
    (void)lpAttributes;
    (void)lpName;
    uint64_t handle = 0;
    handler_NtCreateEvent(&handle, 0x100000, 0, bManualReset ? 1 : 0, bInitialState ? 1 : 0);
    return (void *)(uintptr_t)handle;
}

/* ── SetEvent ────────────────────────────────────────────────── */

WINE_STUB
int SetEvent(void *hEvent)
{
    uint64_t handle = (uint64_t)(uintptr_t)hEvent;
    if (handle == 0) {
        g_last_error = 6; /* ERROR_INVALID_HANDLE */
        return 0;
    }
    uint64_t prev = 0;
    uint64_t status = handler_NtSetEvent(handle, (uint64_t)&prev);
    if (status != 0) {
        g_last_error = 6;
        return 0;
    }
    return 1;
}

/* ── ResetEvent ──────────────────────────────────────────────── */

WINE_STUB
int ResetEvent(void *hEvent)
{
    uint64_t handle = (uint64_t)(uintptr_t)hEvent;
    if (handle == 0) {
        g_last_error = 6;
        return 0;
    }
    uint64_t prev = 0;
    uint64_t status = handler_NtResetEvent(handle, (uint64_t)&prev);
    if (status != 0) {
        g_last_error = 6;
        return 0;
    }
    return 1;
}

/* ── WaitForSingleObject ────────────────────────────────────── */
/*
 * dwMilliseconds: INFINITE=0xFFFFFFFF, else milliseconds.
 * Maps to NtWaitForSingleObject with relative timeout.
 */
WINE_STUB
uint64_t WaitForSingleObject(void *hHandle, uint32_t dwMilliseconds)
{
    uint64_t handle = (uint64_t)(uintptr_t)hHandle;
    if (handle == 0) {
        g_last_error = 6;
        return 0x00000103UL; /* WAIT_ABANDONED - placeholder */
    }

    int64_t timeout_100ns;
    if (dwMilliseconds == 0xFFFFFFFF) {
        uint64_t status = handler_NtWaitForSingleObject(handle, 0, 0);
        if (status == 0) return 0; /* WAIT_OBJECT_0 */
        if (status == 0x00000080UL) return 0x00000102UL; /* WAIT_TIMEOUT */
        return 0x00000103UL; /* WAIT_FAILED */
    }

    if (dwMilliseconds == 0) {
        timeout_100ns = 0;
        uint64_t status = handler_NtWaitForSingleObject(handle, 0, (uint64_t)&timeout_100ns);
        if (status == 0) return 0; /* WAIT_OBJECT_0 */
        return 0x00000102UL; /* WAIT_TIMEOUT */
    }

    /* Convert ms → 100ns (relative = negative) */
    timeout_100ns = -((int64_t)dwMilliseconds * 10000LL);
    uint64_t status = handler_NtWaitForSingleObject(handle, 0, (uint64_t)&timeout_100ns);
    if (status == 0) return 0; /* WAIT_OBJECT_0 */
    if (status == 0x00000080UL) return 0x00000102UL; /* WAIT_TIMEOUT */
    return 0x00000103UL; /* WAIT_FAILED */
}

/* ── CreateMutexA ────────────────────────────────────────────── */

WINE_STUB
void *CreateMutexA(void *lpAttributes, int bInitialOwner, const char *lpName)
{
    (void)lpAttributes;
    (void)lpName;
    (void)bInitialOwner; /* Initial ownership not implemented */
    uint64_t handle = 0;
    handler_NtCreateMutex(&handle, 0x100000, 0);
    return (void *)(uintptr_t)handle;
}

/* ── ReleaseMutex ────────────────────────────────────────────── */

WINE_STUB
int ReleaseMutex(void *hMutex)
{
    uint64_t handle = (uint64_t)(uintptr_t)hMutex;
    uint64_t status = handler_NtReleaseMutex(handle, 0);
    if (status != 0) {
        g_last_error = 6;
        return 0;
    }
    return 1;
}
