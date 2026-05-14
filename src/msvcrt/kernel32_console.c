#define _GNU_SOURCE

#include "kernel32_priv.h"
#include "ntdll_priv.h"
#include "include/nt_constants.h"
#include "../syscall/syscalls_inline.h"

/* Helper: write a static message to stderr via direct syscall */
KERNEL32_STUB
void write_to_stderr(const char *msg)
{
    INLINE_SYSCALL_WRITE_ERR(msg, (size_t)__builtin_strlen(msg));
}


/* ── GetStdHandle ───────────────────────────────────────────── */

KERNEL32_STUB
void *GetStdHandle(int nStdHandle)
{
    switch (nStdHandle) {
    case STD_INPUT_HANDLE:  return FORCE_PTR_RETURN((void *)(uintptr_t)STD_INPUT_HANDLE_VALUE);
    case STD_OUTPUT_HANDLE: return FORCE_PTR_RETURN((void *)(uintptr_t)STD_OUTPUT_HANDLE_VALUE);
    case STD_ERROR_HANDLE:  return FORCE_PTR_RETURN((void *)(uintptr_t)STD_ERROR_HANDLE_VALUE);
    default:                return FORCE_PTR_RETURN(NULL);
    }
}

/* ── WriteFile ──────────────────────────────────────────────── */
/*
 * Do NOT call handler_NtWriteFile from here.  handler_NtWriteFile is
 * compiled with the default (System V) ABI while KERNEL32_STUB uses the
 * Microsoft x64 ABI.  Calling a System V callee from an MS-ABI caller
 * (or vice-versa) corrupts register-based arguments.
 *
 * Instead, resolve the handle ourselves and issue the syscall directly.
 */
KERNEL32_STUB
int WriteFile(void *hFile, const void *lpBuffer, uint32_t nNumberOfBytesToWrite,
              uint32_t *lpNumberOfBytesWritten, void *lpOverlapped)
{
    (void)lpOverlapped;  /* overlapped I/O not yet supported */

    /* Validate the syscall thunk exists (thunk resolution via lookup_thunk) */
    void *thunk = lookup_thunk(NT_SYSCALL_WRITE_FILE);
    if (thunk == NULL) {
        write_to_stderr("my_wine: WriteFile: thunk not found\n");
        return 0;
    }

    uint64_t handle = (uint64_t)(uintptr_t)hFile;
    int fd;

    /* Resolve handle to a Linux FD */
    if (handle == STDIN_HANDLE)  fd = STDIN_FILENO;
    else if (handle == STDOUT_HANDLE) fd = STDOUT_FILENO;
    else if (handle == STDERR_HANDLE) fd = STDERR_FILENO;
    else {
        fd = handle_to_fd(handle);
    }

    if (fd < 0) {
        if (lpNumberOfBytesWritten) *lpNumberOfBytesWritten = 0;
        return 0;
    }

    long res = INLINE_SYSCALL_WRITE(fd, lpBuffer, nNumberOfBytesToWrite);
    if (res < 0) {
        if (lpNumberOfBytesWritten) *lpNumberOfBytesWritten = 0;
        return 0;
    }

    if (lpNumberOfBytesWritten) *lpNumberOfBytesWritten = (uint32_t)res;
    return 1;
}

/* ── ReadFile ───────────────────────────────────────────────── */
/*
 * Same ABI caveat as WriteFile — do the work directly instead of
 * calling handler_NtReadFile.
 */
KERNEL32_STUB
int ReadFile(void *hFile, void *lpBuffer, uint32_t nNumberOfBytesToRead,
             uint32_t *lpNumberOfBytesRead, void *lpOverlapped)
{
    (void)lpOverlapped;  /* overlapped I/O not yet supported */

    /* Validate the syscall thunk exists */
    void *thunk = lookup_thunk(NT_SYSCALL_READ_FILE);
    if (thunk == NULL) {
        write_to_stderr("my_wine: ReadFile: thunk not found\n");
        return 0;
    }

    uint64_t handle = (uint64_t)(uintptr_t)hFile;
    int fd;

    /* Resolve handle to a Linux FD */
    if (handle == STDIN_HANDLE)  fd = STDIN_FILENO;
    else if (handle == STDOUT_HANDLE) fd = STDOUT_FILENO;
    else if (handle == STDERR_HANDLE) fd = STDERR_FILENO;
    else {
        fd = handle_to_fd(handle);
    }

    if (fd < 0) {
        if (lpNumberOfBytesRead) *lpNumberOfBytesRead = 0;
        return 0;
    }

    long res = INLINE_SYSCALL_READ(fd, lpBuffer, nNumberOfBytesToRead);
    if (res < 0) {
        if (lpNumberOfBytesRead) *lpNumberOfBytesRead = 0;
        return 0;
    }

    if (lpNumberOfBytesRead) *lpNumberOfBytesRead = (uint32_t)res;
    return 1;
}
