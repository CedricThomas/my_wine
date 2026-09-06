/*
 * kernel32_sync.c — Synchronization kernel32 stubs
 *
 * CreateEventA, SetEvent, ResetEvent, WaitForSingleObject,
 * CreateMutexA, ReleaseMutex,
 * InitializeCriticalSection, EnterCriticalSection, LeaveCriticalSection,
 * DeleteCriticalSection
 *
 * These map to the corresponding Nt* syscall handlers.
 */

#define _GNU_SOURCE
#include <stdbool.h>
#include <unistd.h>
#include "kernel32_priv.h"

/* ── CreateEventA ────────────────────────────────────────────── */
/*
 * Create an event object. Maps to NtCreateEvent.
 * lpAttributes, lpName ignored → NULL. ManualReset=event_type, initialState.
 */
KERNEL32_STUB
void *CreateEventA(void *lpAttributes, int bManualReset, int bInitialState, const char *lpName)
{
    (void)lpAttributes;
    (void)lpName;
    uint64_t handle = 0;
    handler_NtCreateEvent(&handle, 0x100000, 0, bManualReset ? 1 : 0, bInitialState ? 1 : 0);
    return FORCE_PTR_RETURN((void *)(uintptr_t)handle);
}

/* ── SetEvent ────────────────────────────────────────────────── */

KERNEL32_STUB
int SetEvent(void *hEvent)
{
    uintptr_t handle = (uintptr_t)hEvent;
    if (handle == 0) {
        g_last_error = 6; /* ERROR_INVALID_HANDLE */
        return 0;
    }
    uint64_t prev = 0;
    uint64_t status = handler_NtSetEvent(handle, (uint64_t)(uintptr_t)&prev);
    if (status != 0) {
        g_last_error = 6;
        return 0;
    }
    return 1;
}

/* ── ResetEvent ──────────────────────────────────────────────── */

KERNEL32_STUB
int ResetEvent(void *hEvent)
{
    uintptr_t handle = (uintptr_t)hEvent;
    if (handle == 0) {
        g_last_error = 6;
        return 0;
    }
    uint64_t prev = 0;
    uint64_t status = handler_NtResetEvent(handle, (uint64_t)(uintptr_t)&prev);
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
KERNEL32_STUB
uint64_t WaitForSingleObject(void *hHandle, uint32_t dwMilliseconds)
{
    static uint32_t wait_count = 0;
    uintptr_t handle = (uintptr_t)hHandle;
    uint32_t count = ++wait_count;

    if (debug_level_at_least(1) &&
        ((count & (count - 1)) == 0 || (count % 100000u) == 0)) {
        DEBUG("kernel32: WaitForSingleObject count=%u handle=0x%lx timeout=%u",
              count, (unsigned long)handle, dwMilliseconds);
    }

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
        uint64_t status = handler_NtWaitForSingleObject(handle, 0, (uintptr_t)&timeout_100ns);
        if (status == 0) return 0; /* WAIT_OBJECT_0 */
        return 0x00000102UL; /* WAIT_TIMEOUT */
    }

    /* Convert ms → 100ns (relative = negative) */
    timeout_100ns = -((int64_t)dwMilliseconds * 10000LL);
    uint64_t status = handler_NtWaitForSingleObject(handle, 0, (uintptr_t)&timeout_100ns);
    if (status == 0) return 0; /* WAIT_OBJECT_0 */
    if (status == 0x00000080UL) return 0x00000102UL; /* WAIT_TIMEOUT */
    return 0x00000103UL; /* WAIT_FAILED */
}

/* ── CreateMutexA ────────────────────────────────────────────── */

KERNEL32_STUB
void *CreateMutexA(void *lpAttributes, int bInitialOwner, const char *lpName)
{
    (void)lpAttributes;
    (void)lpName;
    (void)bInitialOwner; /* Initial ownership not implemented */
    uint64_t handle = 0;
    handler_NtCreateMutex(&handle, 0x100000, 0);
    return FORCE_PTR_RETURN((void *)(uintptr_t)handle);
}

/* ── ReleaseMutex ────────────────────────────────────────────── */

KERNEL32_STUB
int ReleaseMutex(void *hMutex)
{
    uintptr_t handle = (uintptr_t)hMutex;
    uint64_t status = handler_NtReleaseMutex(handle, 0);
    if (status != 0) {
        g_last_error = 6;
        return 0;
    }
    return 1;
}

/* ── Critical Section stubs ─────────────────────────────────── */

KERNEL32_STUB
void InitializeCriticalSection(CRITICAL_SECTION *cs)
{
    if (cs) {
        cs->DebugInfo = NULL;
        cs->LockCount = -1;
        cs->RecursionCount = 0;
        cs->OwningThread = 0;
        cs->LockSemaphore = 0;
        cs->SpinCount = 0;
    }
}

KERNEL32_STUB
void EnterCriticalSection(CRITICAL_SECTION *cs)
{
    if (!cs) return;
    int32_t expected = -1;
    if (__atomic_compare_exchange_n(&cs->LockCount, &expected, 0, false,
                                     __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST)) {
        cs->RecursionCount = 1;
        cs->OwningThread = getpid();
        return;
    }
    if (cs->OwningThread == (uint64_t)getpid()) {
        cs->RecursionCount++;
        return;
    }
    /*
     * Slow path: reset the auto-reset event before each wait so this thread
     * does not consume another release signal, then retry the CAS.
     */
    for (;;) {
        if (cs->LockSemaphore == 0) {
            uint64_t handle = 0;
            handler_NtCreateEvent(&handle, 0, 0, 0, 0);
            cs->LockSemaphore = (uintptr_t)handle;
        }
        handler_NtResetEvent(cs->LockSemaphore, 0);
        handler_NtWaitForSingleObject(cs->LockSemaphore, 0, 0);
        expected = -1;
        if (__atomic_compare_exchange_n(&cs->LockCount, &expected, 0, false,
                                         __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST)) {
            cs->RecursionCount = 1;
            cs->OwningThread = getpid();
            return;
        }
    }
}

KERNEL32_STUB
void LeaveCriticalSection(CRITICAL_SECTION *cs)
{
    if (!cs) return;
    cs->RecursionCount--;
    if (cs->RecursionCount == 0) {
        cs->LockCount = -1;
        cs->OwningThread = 0;
        if (cs->LockSemaphore != 0) {
            handler_NtSetEvent(cs->LockSemaphore, 0);
        }
    }
}

KERNEL32_STUB
void DeleteCriticalSection(CRITICAL_SECTION *cs)
{
    if (!cs) return;
    if (cs->LockSemaphore != 0) {
        handler_NtClose(cs->LockSemaphore);
        cs->LockSemaphore = 0;
    }
    __builtin_memset(cs, 0, sizeof(*cs));
}
