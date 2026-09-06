#define _GNU_SOURCE

#include "kernel32_priv.h"
#include "include/debug.h"

/* ── ExitProcess ────────────────────────────────────────────── */

KERNEL32_STUB
void ExitProcess(uint32_t uExitCode)
{
    DEBUG_WRITE_ERR("kernel32: ExitProcess\n",
                    sizeof("kernel32: ExitProcess\n") - 1);
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

KERNEL32_STUB
void *CreateThread(void *lpThreadAttributes, uintptr_t dwStackSize, void *lpStartAddress,
                   void *lpParameter, uint32_t dwCreationFlags, uint32_t *lpThreadId)
{
    uint32_t handle;
    (void)lpThreadAttributes;
    (void)dwStackSize;
    (void)lpStartAddress;
    (void)lpParameter;
    (void)dwCreationFlags;

    if (debug_level_at_least(1)) {
        DEBUG("kernel32: CreateThread start=%p param=%p stack=%lu flags=0x%x",
              lpStartAddress, lpParameter, (unsigned long)dwStackSize, dwCreationFlags);
    }

    handle = (uint32_t)wine_handle_alloc(HANDLE_TYPE_THREAD, NULL);
    if (lpThreadId)
        *lpThreadId = handle;
    return FORCE_PTR_RETURN((void *)(uintptr_t)handle);
}

KERNEL32_STUB
__attribute__((noreturn)) void ExitThread(uint32_t dwExitCode)
{
#ifdef MY_WINE32
    INLINE_SYSCALL_EXIT((int)dwExitCode);
#else
    _exit((int)dwExitCode);
#endif
}
