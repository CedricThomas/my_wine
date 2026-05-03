#define _GNU_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>
#include <time.h>
#include <sys/mman.h>
#include <errno.h>
#include "include/kernel32.h"
#include "include/ntdll.h"
#include "include/syscall/thunk_gen.h"
#include <asm/unistd_64.h>

/* Debug: check stack alignment at critical function entry */
#define ASSERT_STACK_ALIGNED(func_name) do { \
    uintptr_t _sp; \
    __asm__ volatile("mov %%rsp, %0" : "=r"(_sp)); \
    if (_sp % 16 != 0) { \
        const char *msg = "ASSERT: stack not 16-byte aligned at entry to "; \
        syscall(__NR_write, 2, msg, sizeof(msg)-1); \
        syscall(__NR_write, 2, func_name, strlen(func_name)); \
        char hex_buf[20]; \
        int hlen = snprintf(hex_buf, sizeof(hex_buf), " rsp=0x%lx (mod16=%ld)\n", \
                           (unsigned long)_sp, (long)(_sp % 16)); \
        syscall(__NR_write, 2, hex_buf, hlen); \
    } \
} while(0)

/*
 * Direct handler declarations for kernel32 stubs.
 *
 * The syscall thunk in syscall_gen.c assumes the Windows x64 ABI
 * (RCX, RDX, R8, R9) — it is designed for guest PE code, not for
 * native C code running in the host process. Calling a thunk from C
 * using the System V ABI (RDI, RSI, RDX, RCX, R8, R9) results in
 * completely scrambled arguments when the dispatcher reads Windows
 * ABI registers.
 *
 * Therefore, kernel32 stubs call the ntdll handlers directly with
 * properly translated arguments, while still using lookup_thunk()
 * to validate that the syscall is supported.
 */

/* ── GetStdHandle ───────────────────────────────────────────── */

__attribute__((ms_abi))
void *GetStdHandle(int nStdHandle)
{
    ASSERT_STACK_ALIGNED("GetStdHandle");
    switch (nStdHandle) {
    case STD_INPUT_HANDLE:  return (void *)(uintptr_t)0x7FFFFFFFUL;
    case STD_OUTPUT_HANDLE: return (void *)(uintptr_t)0x7FFFFFFEUL;
    case STD_ERROR_HANDLE:  return (void *)(uintptr_t)0x7FFFFFFDUL;
    default:                return NULL;
    }
}

/* ── WriteFile ──────────────────────────────────────────────── */

__attribute__((ms_abi))
int WriteFile(void *hFile, const void *lpBuffer, uint32_t nNumberOfBytesToWrite,
              uint32_t *lpNumberOfBytesWritten, void *lpOverlapped)
{
    ASSERT_STACK_ALIGNED("WriteFile");
    /* Validate the syscall thunk exists (thunk resolution via lookup_thunk) */
    void *thunk = lookup_thunk(0x3D);
    if (thunk == NULL) {
        fprintf(stderr, "my_wine: WriteFile: thunk 0x3D not found\n");
        return 0;
    }

    /*
     * Map kernel32 WriteFile args to NtWriteFile args:
     *   hFile  → file_handle
     *   0      → event  (no event for non-overlapped I/O)
     *   0      → apc    (no APC routine)
     *   0      → context (no APC user context)
     *   lpBuffer  → buffer
     *   nNumberOfBytesToWrite → length
     *   0      → byte_offset (no OVERLAPPED)
     *   lpNumberOfBytesWritten → bytes_written
     */
    uint64_t result = handler_NtWriteFile(
        (uint64_t)(uintptr_t)hFile,
        0,  /* event */
        0,  /* apc */
        0,  /* context */
        (uint64_t)(uintptr_t)lpBuffer,
        (uint64_t)nNumberOfBytesToWrite,
        0,  /* byte_offset */
        (uint64_t)(uintptr_t)lpNumberOfBytesWritten
    );
    (void)lpOverlapped;  /* overlapped I/O not yet supported */
    return result == STATUS_SUCCESS;
}

/* ── ReadFile ───────────────────────────────────────────────── */

__attribute__((ms_abi))
int ReadFile(void *hFile, void *lpBuffer, uint32_t nNumberOfBytesToRead,
             uint32_t *lpNumberOfBytesRead, void *lpOverlapped)
{
    ASSERT_STACK_ALIGNED("ReadFile");
    /* Validate the syscall thunk exists (thunk resolution via lookup_thunk) */
    void *thunk = lookup_thunk(0x3C);
    if (thunk == NULL) {
        fprintf(stderr, "my_wine: ReadFile: thunk 0x3C not found\n");
        return 0;
    }

    /*
     * Map kernel32 ReadFile args to NtReadFile args:
     *   hFile  → file_handle
     *   0      → event
     *   0      → apc
     *   0      → context
     *   lpBuffer  → buffer
     *   nNumberOfBytesToRead → length
     *   0      → byte_offset (no OVERLAPPED)
     *   lpNumberOfBytesRead → bytes_read
     */
    uint64_t result = handler_NtReadFile(
        (uint64_t)(uintptr_t)hFile,
        0,  /* event */
        0,  /* apc */
        0,  /* context */
        (uint64_t)(uintptr_t)lpBuffer,
        (uint64_t)nNumberOfBytesToRead,
        0,  /* byte_offset */
        (uint64_t)(uintptr_t)lpNumberOfBytesRead
    );
    (void)lpOverlapped;  /* overlapped I/O not yet supported */
    return result == STATUS_SUCCESS;
}

/* ── ExitProcess ────────────────────────────────────────────── */

__attribute__((ms_abi))
void ExitProcess(uint32_t uExitCode)
{
    ASSERT_STACK_ALIGNED("ExitProcess");
    /* Validate the syscall thunk exists (thunk resolution via lookup_thunk) */
    void *thunk = lookup_thunk(0x2A);
    if (thunk == NULL) {
        fprintf(stderr, "my_wine: ExitProcess: thunk 0x2A not found\n");
        _exit(1);
    }

    /*
     * Map kernel32 ExitProcess args to NtTerminateProcess args:
     *   0xFFFFFFFF → process_handle (pseudo-handle = current process)
     *   uExitCode  → exit_status
     */
    handler_NtTerminateProcess(0xFFFFFFFF, (uint64_t)uExitCode);
    __builtin_unreachable();
}

/* ── GetProcAddress ─────────────────────────────────────────── */

__attribute__((ms_abi))
void *GetProcAddress(void *hModule, const char *lpProcName)
{
    (void)hModule;
    (void)lpProcName;
    return NULL;
}

/* ── LoadLibraryA ───────────────────────────────────────────── */

__attribute__((ms_abi))
void *LoadLibraryA(const char *lpLibFileName)
{
    (void)lpLibFileName;
    return NULL;
}

/* ── GetModuleHandleA ───────────────────────────────────────── */

__attribute__((ms_abi))
void *GetModuleHandleA(const char *lpModuleName)
{
    (void)lpModuleName;
    return NULL;
}

/* ── lstrlenA ───────────────────────────────────────────────── */

__attribute__((ms_abi))
int lstrlenA(const char *lpString)
{
    return (int)strlen(lpString);
}

/* ── Critical Section stubs ─────────────────────────────────── */

__attribute__((ms_abi))
void InitializeCriticalSection(CRITICAL_SECTION *cs)
{
    if (cs) memset(cs, 0, sizeof(*cs));
}

__attribute__((ms_abi))
void EnterCriticalSection(CRITICAL_SECTION *cs)
{
    if (cs) cs->LockCount++;
}

__attribute__((ms_abi))
void LeaveCriticalSection(CRITICAL_SECTION *cs)
{
    if (cs) cs->RecursionCount--;
}

__attribute__((ms_abi))
void DeleteCriticalSection(CRITICAL_SECTION *cs)
{
    (void)cs;
}

/* ── GetLastError ───────────────────────────────────────────── */

static __thread uint32_t g_last_error = 0;

__attribute__((ms_abi))
uint32_t GetLastError(void)
{
    return g_last_error;
}

/* ── GetStartupInfoA ────────────────────────────────────────── */

__attribute__((ms_abi))
void GetStartupInfoA(STARTUPINFOA *lpStartupInfo)
{
    if (lpStartupInfo) {
        memset(lpStartupInfo, 0, sizeof(*lpStartupInfo));
        lpStartupInfo->cb = sizeof(STARTUPINFOA);
    }
}

/* ── SetUnhandledExceptionFilter ───────────────────────────── */

__attribute__((ms_abi))
void *SetUnhandledExceptionFilter(void *callback)
{
    (void)callback;
    return NULL;
}

/* ── Sleep ──────────────────────────────────────────────────── */

__attribute__((ms_abi))
void Sleep(uint32_t dwMilliseconds)
{
    struct timespec ts;
    ts.tv_sec  = dwMilliseconds / 1000;
    ts.tv_nsec = (dwMilliseconds % 1000) * 1000000L;
    nanosleep(&ts, NULL);
}

/* ── TlsGetValue ───────────────────────────────────────────── */

__attribute__((ms_abi))
void *TlsGetValue(uint32_t dwTlsIndex)
{
    (void)dwTlsIndex;
    return NULL;
}

/* ── VirtualProtect ─────────────────────────────────────────── */

__attribute__((ms_abi))
int VirtualProtect(void *lpAddress, uint32_t dwSize, uint32_t flNewProtect, uint32_t *lpflOldProtect)
{
    fprintf(stderr, "VP: addr=%p sz=0x%x prot=%u\n", lpAddress, dwSize, flNewProtect);
    int prot = 0;
    switch ((int)flNewProtect) {
    case  2: prot = PROT_READ; break;                    /* PAGE_READONLY */
    case  4: prot = PROT_READ | PROT_WRITE; break;      /* PAGE_READWRITE */
    case 16: prot = PROT_EXEC; break;                    /* PAGE_EXECUTE */
    case 32: prot = PROT_READ | PROT_EXEC; break;        /* PAGE_EXECUTE_READ */
    case 64: prot = PROT_READ | PROT_WRITE | PROT_EXEC; break; /* PAGE_EXECUTE_READWRITE */
    default: prot = PROT_READ | PROT_WRITE; break;
    }

    /* Save old protection */
    if (lpflOldProtect) {
        *lpflOldProtect = flNewProtect;
    }

    /* mprotect requires page-aligned addresses */
    size_t page_size = sysconf(_SC_PAGESIZE);
    void *page_start = (void *)((uintptr_t)lpAddress & ~(page_size - 1));
    uintptr_t offset = (uintptr_t)lpAddress - (uintptr_t)page_start;
    size_t total_size = offset + dwSize;
    size_t aligned_size = (total_size + page_size - 1) & ~(size_t)(page_size - 1);

    if (mprotect(page_start, aligned_size, prot) != 0) {
        fprintf(stderr, "VP FAIL: page=%p sz=0x%zx prot=0x%x errno=%d\n", page_start, aligned_size, prot, errno);
        perror("VirtualProtect: mprotect");
        g_last_error = errno;
        return 0;
    }
    fprintf(stderr, "VP OK: page=%p sz=0x%zx\n", page_start, aligned_size);
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

__attribute__((ms_abi))
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
    mbi->AllocationProtect = 4; /* PAGE_READWRITE */
    mbi->RegionSize = 4096;
    mbi->State = 0x2000; /* MEM_COMMIT */
    mbi->Protect = 4; /* PAGE_READWRITE */
    mbi->Type = 0x20000; /* MEM_PRIVATE */

    return sizeof(MEMORY_BASIC_INFORMATION);
}

/* ── __C_specific_handler ──────────────────────────────────── */
/*
 * This is the SEH (Structured Exception Handling) handler used by
 * MSVC for exception dispatch. For our minimal runtime we just
 * return ExceptionContinueSearch (1) to skip the handler.
 */
__attribute__((ms_abi))
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
