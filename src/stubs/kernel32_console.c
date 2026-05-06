#define _GNU_SOURCE

#include "kernel32_priv.h"

/* Helper: write a static message to stderr via direct syscall */
WINE_STUB
void write_to_stderr(const char *msg)
{
    INLINE_SYSCALL_WRITE_ERR(msg, (size_t)__builtin_strlen(msg));
}


/* ── GetStdHandle ───────────────────────────────────────────── */

WINE_STUB
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

WINE_STUB
int WriteFile(void *hFile, const void *lpBuffer, uint32_t nNumberOfBytesToWrite,
              uint32_t *lpNumberOfBytesWritten, void *lpOverlapped)
{
    /* Validate the syscall thunk exists (thunk resolution via lookup_thunk) */
    void *thunk = lookup_thunk(NT_SYSCALL_WRITE_FILE);
    if (thunk == NULL) {
        write_to_stderr("my_wine: WriteFile: thunk not found\n");
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

WINE_STUB
int ReadFile(void *hFile, void *lpBuffer, uint32_t nNumberOfBytesToRead,
             uint32_t *lpNumberOfBytesRead, void *lpOverlapped)
{
    /* Validate the syscall thunk exists (thunk resolution via lookup_thunk) */
    void *thunk = lookup_thunk(NT_SYSCALL_READ_FILE);
    if (thunk == NULL) {
        write_to_stderr("my_wine: ReadFile: thunk not found\n");
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
