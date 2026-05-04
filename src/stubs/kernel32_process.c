#define _GNU_SOURCE

#include <stdio.h>
#include "kernel32_priv.h"

/* ── ExitProcess ────────────────────────────────────────────── */

WINE_STUB
void ExitProcess(uint32_t uExitCode)
{
    /* Validate the syscall thunk exists (thunk resolution via lookup_thunk) */
    void *thunk = lookup_thunk(NT_SYSCALL_TERMINATE_PROCESS);
    if (thunk == NULL) {
        write_to_stderr("my_wine: ExitProcess: thunk not found\n");
        INLINE_SYSCALL_EXIT_GROUP(1);
    }

    /*
     * Map kernel32 ExitProcess args to NtTerminateProcess args:
     *   0xFFFFFFFF → process_handle (pseudo-handle = current process)
     *   uExitCode  → exit_status
     */
    handler_NtTerminateProcess(HANDLE_CURRENT_PROCESS, (uint64_t)uExitCode);
    __builtin_unreachable();
}

/* ── GetStartupInfoA ────────────────────────────────────────── */

WINE_STUB
void GetStartupInfoA(STARTUPINFOA *lpStartupInfo)
{
    if (lpStartupInfo) {
        __builtin_memset(lpStartupInfo, 0, sizeof(*lpStartupInfo));
        lpStartupInfo->cb = sizeof(STARTUPINFOA);
    }
}

/* ── SetUnhandledExceptionFilter ───────────────────────────── */

WINE_STUB
void *SetUnhandledExceptionFilter(void *callback)
{
    (void)callback;
    return NULL;
}

/* ── Sleep ──────────────────────────────────────────────────── */

WINE_STUB
void Sleep(uint32_t dwMilliseconds)
{
    char buf[64];
    int len = sprintf(buf, "TRACE: Sleep(%u)\n", dwMilliseconds);
    INLINE_SYSCALL_WRITE_ERR(buf, (size_t)len);

    struct timespec ts;
    ts.tv_sec  = dwMilliseconds / 1000;
    ts.tv_nsec = (dwMilliseconds % 1000) * 1000000L;
    /* Call nanosleep syscall directly (avoids ABI mismatch with libc wrapper) */
    (void)INLINE_SYSCALL_NANOSLEEP(&ts, (const void *)0);
}
