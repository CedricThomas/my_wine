#define _GNU_SOURCE

#include <string.h>
#include "kernel32_priv.h"
#include "include/wine_abi.h"
#include "include/nt_constants.h"

/*
 * _acmdln is defined in crt_globals.c. We use a weak declaration so that
 * build targets that exclude crt_*.o (e.g. test_syscall_dispatch) don't
 * get an undefined reference. When weak, _acmdln is NULL if not linked.
 */
extern char *_acmdln __attribute__((weak));

/* Thread-local last-error code */
__thread uint32_t g_last_error = 0;

/* ── lstrlenA ───────────────────────────────────────────────── */

WINE_STUB
int lstrlenA(const char *lpString)
{
    const char *s = lpString;
    while (*s) s++;
    return (int)(s - lpString);
}

/* ── lstrcpyA ───────────────────────────────────────────────── */

WINE_STUB
char *lstrcpyA(char *dest, const char *src)
{
    char *d = dest;
    const char *s = src;
    if (d == NULL || s == NULL) return FORCE_PTR_RETURN(NULL);
    do {
        *d++ = *s++;
    } while (s[-1] != '\0');
    return FORCE_PTR_RETURN(dest);
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
    return FORCE_PTR_RETURN(NULL);
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
        g_last_error = ERROR_INVALID_PARAMETER;
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
        g_last_error = ERROR_INVALID_PARAMETER;
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
        g_last_error = ERROR_INVALID_PARAMETER;
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
        g_last_error = ERROR_ACCESS_DENIED;
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
        g_last_error = ERROR_INSUFFICIENT_BUFFER;
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

/* ── GetCommandLineA ───────────────────────────────────────── */
/*
 * Returns the command-line string for the current process.
 * In our runtime, _acmdln is set from main.c before the PE entry.
 * _acmdln is a weak symbol — may be NULL in build targets that don't
 * link crt_globals.o.
 */
WINE_STUB
const char *GetCommandLineA(void)
{
    return FORCE_PTR_RETURN(_acmdln ? _acmdln : "");
}

/* ── GetEnvironmentStringsA ────────────────────────────────── */
/*
 * Returns the environment block for the current process.
 * Returns NULL — most PE startup code only reads this to verify
 * the environment is accessible, and the CRT uses __initenv instead.
 */
WINE_STUB
char *GetEnvironmentStringsA(void)
{
    return FORCE_PTR_RETURN(NULL);
}

/* ── IsDBCSLeadByteEx ──────────────────────────────────────── */
WINE_STUB
int IsDBCSLeadByteEx(uint16_t code_page, uint8_t byte)
{
    (void)code_page;
    (void)byte;
    return 0;
}

/* ── MultiByteToWideChar ───────────────────────────────────── */
WINE_STUB
int MultiByteToWideChar(uint32_t code_page, uint32_t dw_flags,
                        const char *lpMultiByteStr, int cbMultiByteChar,
                        void *lpWideCharStr, int cchWideChar)
{
    (void)code_page;
    (void)dw_flags;
    (void)lpMultiByteStr;
    (void)cbMultiByteChar;
    (void)lpWideCharStr;
    (void)cchWideChar;
    return 0;
}

/* ── WideCharToMultiByte ───────────────────────────────────── */
WINE_STUB
int WideCharToMultiByte(uint32_t code_page, uint32_t dw_flags,
                        const void *lpWideCharStr, int cchWideChar,
                        char *lpMultiByteStr, int cbMultiByteChar,
                        void *lpDefaultChar, void *lpUsedDefaultChar)
{
    (void)code_page;
    (void)dw_flags;
    (void)lpWideCharStr;
    (void)cchWideChar;
    (void)lpMultiByteStr;
    (void)cbMultiByteChar;
    (void)lpDefaultChar;
    (void)lpUsedDefaultChar;
    return 0;
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

/*
 * Test stubs for validating FORCE_PTR_RETURN macro behavior.
 * Used to verify that pointer-returning stubs correctly force values
 * into RAX for guest code consumption.
 */
WINE_STUB void *test_return_ptr(void) { return FORCE_PTR_RETURN((void *)0x12345678UL); }
WINE_STUB void *test_return_ptr_arg(void *arg) { return FORCE_PTR_RETURN(arg ? arg : (void *)0xdeadbeefUL); }
