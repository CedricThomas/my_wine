#define _GNU_SOURCE

#include "kernel32_priv.h"
#include "include/debug.h"

/* ── ExitProcess ────────────────────────────────────────────── */

KERNEL32_STUB
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

KERNEL32_STUB
void GetStartupInfoA(STARTUPINFOA *lpStartupInfo)
{
    if (lpStartupInfo) {
        __builtin_memset(lpStartupInfo, 0, sizeof(*lpStartupInfo));
        lpStartupInfo->cb = sizeof(STARTUPINFOA);
    }
}

/* ── SetUnhandledExceptionFilter ───────────────────────────── */

KERNEL32_STUB
void *SetUnhandledExceptionFilter(void *callback)
{
    (void)callback;
    return FORCE_PTR_RETURN(NULL);
}

/* ── Sleep ──────────────────────────────────────────────────── */

KERNEL32_STUB
void Sleep(uint32_t dwMilliseconds)
{
    /* Inline formatting — avoids glibc sprintf which accesses vDSO via GS. */
    char buf[64];
    int i = 0;
    if (debug_is_enabled()) {
        const char prefix[] = "TRACE: Sleep(";
        for (int j = 0; j < (int)(sizeof(prefix) - 1); j++) buf[i++] = prefix[j];
        /* Write dwMilliseconds as decimal string */
        char tmp[16];
        int t = 0;
        uint32_t v = dwMilliseconds;
        do { tmp[t++] = '0' + (v % 10); v /= 10; } while (v > 0);
        while (t > 0) buf[i++] = tmp[--t];
        buf[i++] = ')';
        buf[i++] = '\n';
    }
    if (i > 0)
        INLINE_SYSCALL_WRITE_ERR(buf, (size_t)i);

    struct timespec ts;
    ts.tv_sec  = dwMilliseconds / 1000;
    ts.tv_nsec = (dwMilliseconds % 1000) * 1000000L;
    /* Call nanosleep syscall directly (avoids ABI mismatch with libc wrapper) */
    (void)INLINE_SYSCALL_NANOSLEEP(&ts, (const void *)0);
}
