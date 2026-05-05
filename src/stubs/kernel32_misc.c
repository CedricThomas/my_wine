#define _GNU_SOURCE

#include <unistd.h>
#include "kernel32_priv.h"

/* Thread-local last-error code */
__thread uint32_t g_last_error = 0;

/* ── lstrlenA ───────────────────────────────────────────────── */

WINE_STUB
int lstrlenA(const char *lpString)
{
    return (int)__builtin_strlen(lpString);
}

/* ── Critical Section stubs ─────────────────────────────────── */

WINE_STUB
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

WINE_STUB
void EnterCriticalSection(CRITICAL_SECTION *cs)
{
    if (!cs) return;
    int32_t expected = -1;
    if (__atomic_compare_exchange_n(&cs->LockCount, &expected, 0, 0,
                                     __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST)) {
        cs->RecursionCount = 1;
        cs->OwningThread = getpid();
        return;
    }
    // CAS failed — contention
    if (cs->OwningThread == (uint64_t)getpid()) {
        // Recursive entry by same thread
        cs->RecursionCount++;
        return;
    }
    // Slow path: wait for the owner to release
    if (cs->LockSemaphore == 0) {
        // Lazy-create event (initially non-signaled, auto-reset)
        handler_NtCreateEvent(&cs->LockSemaphore, 0, 0, 0, 0);
    }
    handler_NtWaitForSingleObject(cs->LockSemaphore, 0, 0);
    cs->LockCount = 0;
    cs->RecursionCount = 1;
    cs->OwningThread = getpid();
}

WINE_STUB
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

WINE_STUB
void DeleteCriticalSection(CRITICAL_SECTION *cs)
{
    if (!cs) return;
    if (cs->LockSemaphore != 0) {
        handler_NtClose(cs->LockSemaphore);
        cs->LockSemaphore = 0;
    }
    __builtin_memset(cs, 0, sizeof(*cs));
}

/* ── GetLastError ───────────────────────────────────────────── */

WINE_STUB
uint32_t GetLastError(void)
{
    return g_last_error;
}

/* ── TlsGetValue ───────────────────────────────────────────── */

WINE_STUB
void *TlsGetValue(uint32_t dwTlsIndex)
{
    (void)dwTlsIndex;
    return NULL;
}

/* ── GetSystemTimeAsFileTime ───────────────────────────────── */
/*
 * Maps to NtQuerySystemTime. Returns current system time as a FILETIME
 * (100ns since 1601-01-01 UTC).
 */
WINE_STUB
void GetSystemTimeAsFileTime(FILETIME *lpSystemTime)
{
    if (lpSystemTime == NULL) {
        g_last_error = 87; /* ERROR_INVALID_PARAMETER */
        return;
    }

    uint64_t filetime;
    handler_NtQuerySystemTime((uint64_t)&filetime);

    lpSystemTime->dwLowDateTime  = (uint32_t)(filetime & 0xFFFFFFFF);
    lpSystemTime->dwHighDateTime = (uint32_t)(filetime >> 32);
}

/* ── QueryPerformanceCounter ───────────────────────────────── */
/*
 * Maps to NtQueryPerformanceCounter. Returns a high-resolution
 * performance counter value as 100ns ticks.
 */
WINE_STUB
int QueryPerformanceCounter(LARGE_INTEGER *lpPerformanceCount)
{
    if (lpPerformanceCount == NULL) {
        g_last_error = 87; /* ERROR_INVALID_PARAMETER */
        return 0;
    }

    uint64_t counter;
    handler_NtQueryPerformanceCounter((uint64_t)&counter);

    lpPerformanceCount->QuadPart = (int64_t)counter;
    return 1;
}

/* ── QueryPerformanceFrequency ─────────────────────────────── */
/*
 * Maps to NtQueryPerformanceFrequency. Returns the performance counter
 * frequency (10^7 = 100ns resolution).
 */
WINE_STUB
int QueryPerformanceFrequency(LARGE_INTEGER *lpFrequency)
{
    if (lpFrequency == NULL) {
        g_last_error = 87; /* ERROR_INVALID_PARAMETER */
        return 0;
    }

    uint64_t freq;
    handler_NtQueryPerformanceFrequency((uint64_t)&freq);

    lpFrequency->QuadPart = (int64_t)freq;
    return 1;
}

/* ── VirtualProtect ─────────────────────────────────────────── */

WINE_STUB
int VirtualProtect(void *lpAddress, uint32_t dwSize, uint32_t flNewProtect, uint32_t *lpflOldProtect)
{
    int prot = map_protect(flNewProtect);

    /* Save old protection */
    if (lpflOldProtect) {
        *lpflOldProtect = flNewProtect;
    }

    /* mprotect requires page-aligned addresses */
    size_t page_size = PAGE_SIZE; /* constant instead of sysconf(_SC_PAGESIZE) to avoid libc */
    void *page_start = (void *)((uintptr_t)lpAddress & ~(page_size - 1));
    uintptr_t offset = (uintptr_t)lpAddress - (uintptr_t)page_start;
    size_t total_size = offset + dwSize;
    size_t aligned_size = (total_size + page_size - 1) & ~(size_t)(page_size - 1);

    if (sysv_mprotect(page_start, aligned_size, prot) != 0) {
        write_to_stderr("my_wine: VirtualProtect: mprotect failed\n");
        g_last_error = 1; /* fixed error code instead of errno */
        return 0;
    }
    return 1;
}

/* ── VirtualQuery ───────────────────────────────────────────── */

/* MEMORY_BASIC_INFORMATION (x64, 48 bytes) */
typedef struct {
    void    *BaseAddress;
    void    *AllocationBase;
    uint32_t AllocationProtect;
    uint32_t __unused1;
    uint64_t RegionSize;
    uint32_t State;
    uint32_t Protect;
    uint32_t Type;
    uint32_t __unused2;
} MEMORY_BASIC_INFORMATION;

WINE_STUB
uint64_t VirtualQuery(void *lpAddress, void *lpBuffer, uint32_t dwLength)
{
    if (!lpAddress || !lpBuffer) {
        return 0;
    }

    if (dwLength < sizeof(MEMORY_BASIC_INFORMATION)) {
        g_last_error = 122; /* ERROR_INSUFFICIENT_BUFFER */
        return 0;
    }

    MEMORY_BASIC_INFORMATION *mbi = (MEMORY_BASIC_INFORMATION *)lpBuffer;
    mbi->BaseAddress = lpAddress;
    mbi->AllocationBase = lpAddress;
    mbi->AllocationProtect = PAGE_READWRITE;
    mbi->RegionSize = PAGE_SIZE;
    mbi->State = MEM_COMMIT;
    mbi->Protect = PAGE_READWRITE;
    mbi->Type = MEM_PRIVATE;

    return sizeof(MEMORY_BASIC_INFORMATION);
}

/* ── __C_specific_handler ──────────────────────────────────── */
/*
 * This is the SEH (Structured Exception Handling) handler used by
 * MSVC for exception dispatch. For our minimal runtime we just
 * return ExceptionContinueSearch (1) to skip the handler.
 */
WINE_STUB
uint64_t __C_specific_handler(uint64_t exception_record, uint64_t establisher_frame,
                               uint64_t context_record, uint64_t dispatcher_context,
                               uint64_t image_base, uint64_t module_data,
                               uint64_t count, uint64_t handlers, uint64_t lang_handler,
                               uint64_t user_handler)
{
    (void)exception_record;
    (void)establisher_frame;
    (void)context_record;
    (void)dispatcher_context;
    (void)image_base;
    (void)module_data;
    (void)count;
    (void)handlers;
    (void)lang_handler;
    (void)user_handler;
    /*
     * Return values:
     *   0 = ExceptionContinueExecution
     *   1 = ExceptionContinueSearch
     *   2 = ExceptionNestedException
     *   3 = ExceptionCollidedUnwind
     */
    return 1; /* ExceptionContinueSearch — skip this handler */
}
