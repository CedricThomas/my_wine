#define _GNU_SOURCE

#include <string.h>
#include "kernel32_priv.h"
#include "msvcrt_priv.h"
#include "include/wine_abi.h"
#include "include/nt_constants.h"

#define AT_FDCWD ((long)-100)

/*
 * g_crt is declared in include/crt.h (included via msvcrt_priv.h) and defined
 * in crt_globals.c. For build targets that exclude crt_*.o (e.g. test_syscall_dispatch),
 * we declare it as weak so the build doesn't fail with undefined reference.
 * At runtime, we check the address to see if g_crt was actually linked.
 */
extern wine_crt_state_t g_crt __attribute__((weak));
extern void *g_argv_page;

/* Thread-local last-error code */
/* In PE32 mode, __thread uses GS-relative access but GS=0 (only FS is set to TEB).
 * Use a plain global — PE32 is single-threaded so no contention risk. */
uint32_t g_last_error = 0;

/* 32-bit: simple FD storage for CreateFileA (bypasses pthread handle manager) */
#if defined(__i386__)
int createfile_fds_32[64] = {0};
int createfile_fd_count_32 = 0;
#endif

/* ── VirtualAlloc tracking (for VirtualFree with size=0) ──────── */
#define MAX_VM_ALLOCS 64

typedef struct {
    uint64_t base;
    uint64_t size;
} vm_alloc_entry_t;

static vm_alloc_entry_t vm_allocs[MAX_VM_ALLOCS];
static int vm_alloc_count = 0;

static int vm_alloc_find(uint64_t base)
{
    int i;
    for (i = 0; i < vm_alloc_count; i++) {
        if (vm_allocs[i].base == base)
            return i;
    }
    return -1;
}

static void vm_alloc_add(uint64_t base, uint64_t size)
{
    if (vm_alloc_count >= MAX_VM_ALLOCS) return;
    vm_allocs[vm_alloc_count].base = base;
    vm_allocs[vm_alloc_count].size = size;
    vm_alloc_count++;
}

static void vm_alloc_remove(int idx)
{
    if (idx < 0 || idx >= vm_alloc_count) return;
    vm_allocs[idx] = vm_allocs[vm_alloc_count - 1];
    vm_alloc_count--;
}

/* ── lstrlenA ───────────────────────────────────────────────── */

KERNEL32_STUB
int lstrlenA(const char *lpString)
{
    const char *s = lpString;
    while (*s) s++;
    return (int)(s - lpString);
}

/* ── lstrcpyA ───────────────────────────────────────────────── */

KERNEL32_STUB
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

/* ── lstrcatA ───────────────────────────────────────────────── */

KERNEL32_STUB
char *lstrcatA(char *dest, const char *src)
{
    char *d = dest;
    const char *s = src;
    if (d == NULL) return FORCE_PTR_RETURN(NULL);
    if (s == NULL) return FORCE_PTR_RETURN(dest);
    while (*d) d++;
    while (*s) { *d++ = *s++; }
    *d = '\0';
    return FORCE_PTR_RETURN(dest);
}

/* ── GetLastError ───────────────────────────────────────────── */

KERNEL32_STUB
uint32_t GetLastError(void)
{
    return g_last_error;
}

/* ── TlsGetValue ───────────────────────────────────────────── */

KERNEL32_STUB
void *TlsGetValue(uint32_t dwTlsIndex)
{
    (void)dwTlsIndex;
    return FORCE_PTR_RETURN(NULL);
}

/* ── GetSystemTimeAsFileTime ───────────────────────────────── */
/*
 * Returns current system time as a FILETIME (100ns since 1601-01-01 UTC).
 *
 * Inlines the logic directly (instead of calling handler_NtQuerySystemTime)
 * to avoid a uint64_t parameter passing through the handler ABI in 32-bit
 * mode, where the handler's inline asm can clobber callee-saved registers.
 */
KERNEL32_STUB
void GetSystemTimeAsFileTime(FILETIME *lpSystemTime)
{
    if (lpSystemTime == NULL) {
        g_last_error = ERROR_INVALID_PARAMETER;
        return;
    }

    struct timespec ts;
    if (INLINE_SYSCALL_CLOCK_GETTIME(CLOCK_REALTIME, &ts) != 0) {
        return;
    }
    uint64_t filetime = (uint64_t)ts.tv_sec * 10000000ULL + (uint64_t)ts.tv_nsec / 100ULL;
    filetime += 116444736000000000ULL; /* 1601→1970 epoch offset in 100ns */

    lpSystemTime->dwLowDateTime  = (uint32_t)(filetime & 0xFFFFFFFF);
    lpSystemTime->dwHighDateTime = (uint32_t)(filetime >> 32);
}

/* ── QueryPerformanceCounter ───────────────────────────────── */
/*
 * Returns a high-resolution performance counter value as 100ns ticks.
 *
 * Inlines the logic directly (instead of calling handler_NtQueryPerformanceCounter)
 * to avoid a uint64_t parameter passing through the handler ABI in 32-bit
 * mode, where the handler's inline asm can clobber callee-saved registers.
 */
KERNEL32_STUB
int QueryPerformanceCounter(LARGE_INTEGER *lpPerformanceCount)
{
    if (lpPerformanceCount == NULL) {
        g_last_error = ERROR_INVALID_PARAMETER;
        return 0;
    }

    struct timespec ts;
    if (INLINE_SYSCALL_CLOCK_GETTIME(CLOCK_MONOTONIC, &ts) != 0) {
        return 0;
    }
    uint64_t counter = (uint64_t)ts.tv_sec * 10000000ULL + (uint64_t)ts.tv_nsec / 100ULL;
    lpPerformanceCount->QuadPart = (int64_t)counter;
    return 1;
}

/* ── QueryPerformanceFrequency ─────────────────────────────── */
/*
 * Returns the performance counter frequency (10^7 = 100ns resolution).
 *
 * Inlines the constant directly (instead of calling handler_NtQueryPerformanceFrequency)
 * to avoid a uint64_t parameter passing through the handler ABI in 32-bit mode.
 */
KERNEL32_STUB
int QueryPerformanceFrequency(LARGE_INTEGER *lpFrequency)
{
    if (lpFrequency == NULL) {
        g_last_error = ERROR_INVALID_PARAMETER;
        return 0;
    }

    lpFrequency->QuadPart = (int64_t)10000000ULL; /* 10^7 = 100ns resolution */
    return 1;
}

/* ── VirtualProtect ─────────────────────────────────────────── */

KERNEL32_STUB
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

KERNEL32_STUB
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
 * In 32-bit mode, g_argv_page is allocated with MAP_32BIT (below 4GB)
 * and holds the PE path at offset 0, so the base pointer IS the string.
 * In 64-bit mode, g_crt.acmdln (from crt_globals) is fine since all addresses
 * are accessible to the guest.
 */
KERNEL32_STUB
const char *GetCommandLineA(void)
{
#ifdef __i386__
    if (g_argv_page) return FORCE_PTR_RETURN((const char *)g_argv_page);
    return FORCE_PTR_RETURN("");
#else
    if ((uintptr_t)&g_crt != 0 && g_crt.acmdln)
        return FORCE_PTR_RETURN(g_crt.acmdln);
    return FORCE_PTR_RETURN("");
#endif
}

/* ── GetEnvironmentStringsA ────────────────────────────────── */
/*
 * Returns the environment block for the current process.
 * Returns NULL — most PE startup code only reads this to verify
 * the environment is accessible, and the CRT uses __initenv instead.
 */
KERNEL32_STUB
char *GetEnvironmentStringsA(void)
{
    return FORCE_PTR_RETURN(NULL);
}

/* ── IsDBCSLeadByteEx ──────────────────────────────────────── */
KERNEL32_STUB
int IsDBCSLeadByteEx(uint16_t code_page, uint8_t byte)
{
    (void)code_page;
    (void)byte;
    return 0;
}

/* ── MultiByteToWideChar ───────────────────────────────────── */
KERNEL32_STUB
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
KERNEL32_STUB
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
KERNEL32_STUB
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

/* ── CloseHandle ──────────────────────────────────────────────── */
KERNEL32_STUB
int CloseHandle(void *hObject)
{
    uint64_t handle = (uint64_t)(uintptr_t)hObject;

    /* Pseudo-handles are never closeable — return success */
    if (handle == STDIN_HANDLE || handle == STDOUT_HANDLE || handle == STDERR_HANDLE)
        return 1;

    /* Small integer handles (real fds 0,1,2) — never close */
    if (handle <= 2)
        return 1;

#if defined(__i386__)
    /* 32-bit: check our simple handle table */
    int idx = (int)handle - 3;
    if (idx >= 0 && idx < createfile_fd_count_32) {
        int fd = createfile_fds_32[idx];
        if (fd >= 0) {
            INLINE_SYSCALL_CLOSE(fd);
            createfile_fds_32[idx] = -1;
            return 1;
        }
    }
#endif

    uint64_t status = handler_NtClose(handle);
    return status == STATUS_SUCCESS;
}

/* ── CreateFileA ─────────────────────────────────────────────── */
KERNEL32_STUB
void *CreateFileA(const char *lpFileName, uint32_t dwDesiredAccess,
                  uint32_t dwShareMode, void *lpSecurityAttributes,
                  uint32_t dwCreationDisposition, uint32_t dwFlagsAndAttributes,
                  void *hTemplateFile)
{
    (void)dwShareMode;
    (void)lpSecurityAttributes;
    (void)dwFlagsAndAttributes;
    (void)hTemplateFile;

    if (lpFileName == NULL)
        return FORCE_PTR_RETURN(INVALID_HANDLE_VALUE);

    /*
     * Normalize Windows path: strip drive letter ("C:\") and convert \\ to /
     * so that "C:\tmp\file.dat" becomes "/tmp/file.dat".
     */
    char path_buf[1024];
    const char *p = lpFileName;
    char *d = path_buf;

    /* Skip drive letter prefix like "C:\" */
    if (p[0] != '\0' && p[1] == ':') {
        p += 2;
        if (*p == '\\' || *p == '/') p++; /* skip root separator */
    }

    /* Copy, converting backslashes to forward slashes */
    int max_len = (int)__builtin_strlen(p);
    if (max_len > 1022) max_len = 1022;
    int i;
    for (i = 0; i < max_len; i++) {
        char c = p[i];
        if (c == '\\') c = '/';
        *d++ = c;
    }
    *d = '\0';

    /* Ensure path starts with / */
    if (path_buf[0] != '/') {
        /* Shift bytes right by 1 to make room for '/' at front.
         * Can't use memmove (libc) after GS base switch in 32-bit. */
        int len = (int)(d - path_buf);  /* includes '\0' */
        int k;
        for (k = len; k >= 0; k--)
            path_buf[k + 1] = path_buf[k];
        path_buf[0] = '/';
    }

    /* Map desired access to Linux open flags */
    int oflags = 0;
    uint64_t desired = (uint64_t)dwDesiredAccess;
    if (desired & GENERIC_WRITE)
        oflags = 2;  /* O_RDWR */
    else
        oflags = 0;  /* O_RDONLY */

    /* Map creation disposition to Linux flags
     * O_RDONLY=0, O_WRONLY=1, O_RDWR=2
     * O_CREAT=0100, O_EXCL=0200, O_TRUNC=01000
     * These are defined in <asm-generic/fcntl.h> and <bits/fcntl-linux.h>
     */
    switch (dwCreationDisposition) {
    case 1: /* CREATE_NEW          */ oflags |= 0200 | 0100; break;   /* O_EXCL | O_CREAT */
    case 2: /* CREATE_ALWAYS       */ oflags |= 01000 | 0100; break;  /* O_TRUNC | O_CREAT */
    case 3: /* OPEN_EXISTING       */ break;  /* no extra flags */
    case 4: /* OPEN_ALWAYS         */ oflags |= 0100; break;           /* O_CREAT */
    case 5: /* TRUNCATE_EXISTING   */ oflags |= 01000; break;          /* O_TRUNC */
    default: break;
    }

    /* Open the file */
    long res = INLINE_SYSCALL_OPENAT(AT_FDCWD, path_buf, oflags, 0644);
    int fd = (int)res;
    if (fd < 0)
        return FORCE_PTR_RETURN(INVALID_HANDLE_VALUE);

    /* Convert to a wine handle */
    uint64_t handle;
#if defined(__i386__)
    /* In 32-bit after GS base switch, pthread_mutex_lock crashes.
     * Use a simple counter-based handle allocation as fallback.
     * Handles 0-2 are reserved for stdin/stdout/stderr.
     * Handle 3+ are mapped to FDs directly.
     */
    int idx = createfile_fd_count_32;
    if (idx >= 64) {
        INLINE_SYSCALL_CLOSE(fd);
        return FORCE_PTR_RETURN(INVALID_HANDLE_VALUE);
    }
    createfile_fds_32[idx] = fd;
    handle = (uint64_t)(3 + idx);
    createfile_fd_count_32++;
#else
    handle = fd_to_handle(fd);
#endif
    if (handle == 0) {
        INLINE_SYSCALL_CLOSE(fd);
        return FORCE_PTR_RETURN(INVALID_HANDLE_VALUE);
    }

    return FORCE_PTR_RETURN((void *)(uintptr_t)handle);
}

/* ── DeleteFileA ─────────────────────────────────────────────── */
KERNEL32_STUB
int DeleteFileA(const char *lpFileName)
{
    if (lpFileName == NULL)
        return 0;

    /* Normalize Windows path same as CreateFileA */
    char path_buf[1024];
    const char *p = lpFileName;
    char *d = path_buf;

    /* Skip drive letter prefix like "C:\" */
    if (p[0] != '\0' && p[1] == ':') {
        p += 2;
        if (*p == '\\' || *p == '/') p++;
    }

    /* Copy, converting backslashes to forward slashes */
    int max_len = (int)__builtin_strlen(p);
    if (max_len > 1022) max_len = 1022;
    int i;
    for (i = 0; i < max_len; i++) {
        char c = p[i];
        if (c == '\\') c = '/';
        *d++ = c;
    }
    *d = '\0';

    /* Ensure path starts with / */
    if (path_buf[0] != '/') {
        int len = (int)(d - path_buf);
        int k;
        for (k = len; k >= 0; k--)
            path_buf[k + 1] = path_buf[k];
        path_buf[0] = '/';
    }

    /* Use unlinkat(AT_FDCWD, path, 0) via syscall to avoid libc dependency */
    long res = INLINE_SYSCALL_UNLINKAT(AT_FDCWD, path_buf, 0);
    if (res != 0) {
        g_last_error = ERROR_FILE_NOT_FOUND;
        return 0;
    }
    return 1;
}

/* ── VirtualAlloc ────────────────────────────────────────────── */
KERNEL32_STUB
void *VirtualAlloc(void *lpAddress,
#if defined(__i386__)
                   uint32_t dwSize,
#else
                   uint64_t dwSize,
#endif
                   uint32_t flAllocationType, uint32_t flProtect)
{
    if (dwSize == 0)
        return FORCE_PTR_RETURN(NULL);

    uint64_t base = (uint64_t)(uintptr_t)lpAddress;
    uint64_t region_size = (uint64_t)dwSize;

    uint64_t status = handler_NtAllocateVirtualMemory(
        HANDLE_CURRENT_PROCESS,
        &base,
        0,       /* zero_bits */
        &region_size,
        (uint64_t)flAllocationType,
        (uint64_t)flProtect
    );

    if (status != STATUS_SUCCESS)
        return FORCE_PTR_RETURN(NULL);

    void *result = (void *)(uintptr_t)base;
    vm_alloc_add(base, region_size);
    return FORCE_PTR_RETURN(result);
}

/* ── VirtualFree ─────────────────────────────────────────────── */
KERNEL32_STUB
int VirtualFree(void *lpAddress,
#if defined(__i386__)
                uint32_t dwSize,
#else
                uint64_t dwSize,
#endif
                uint32_t dwFreeType)
{
    if (lpAddress == NULL) {
        return 0;
    }

    uint64_t base = (uint64_t)(uintptr_t)lpAddress;
    uint64_t region_size = (uint64_t)dwSize;

    /* When MEM_RELEASE is used with size 0, find the actual allocation size */
    if ((dwFreeType & 0x8000) && region_size == 0) {  /* MEM_RELEASE */
        int idx = vm_alloc_find(base);
        if (idx >= 0) {
            region_size = vm_allocs[idx].size;
        } else {
            /* Try section views as fallback */
            int vidx = find_view(lpAddress);
            if (vidx >= 0)
                region_size = (uint64_t)ko_view(vidx)->size;
            else {
                return 0;  /* can't find the mapping */
            }
        }
    }

    uint64_t status = handler_NtFreeVirtualMemory(
        HANDLE_CURRENT_PROCESS,
        &base,
        &region_size,
        (uint64_t)dwFreeType
    );

    if (status == STATUS_SUCCESS) {
        int idx = vm_alloc_find(base);
        if (idx >= 0)
            vm_alloc_remove(idx);
    }

    return status == STATUS_SUCCESS;
}

/* ── test stubs ──────────────────────────────────────────────── */
/*
 * Test stubs for validating FORCE_PTR_RETURN macro behavior.
 * Used to verify that pointer-returning stubs correctly force values
 * into RAX for guest code consumption.
 */
KERNEL32_STUB void *test_return_ptr(void) { return FORCE_PTR_RETURN((void *)0x12345678UL); }
KERNEL32_STUB void *test_return_ptr_arg(void *arg) { return FORCE_PTR_RETURN(arg ? arg : (void *)0xdeadbeefUL); }
