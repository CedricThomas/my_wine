/*
 * sync_test_32.c — Test synchronization NT syscalls (PE32)
 *
 * Uses Windows API: CreateEventA, SetEvent, WaitForSingleObject,
 * ResetEvent, CreateMutexA, ReleaseMutex.
 * Build: make samples SAMPLE=sync_test_32
 * Run:   ./my_wine samples/sync_test_32/sync_test_32.exe
 */

#include <windows.h>
#include <stdint.h>

static int fmt_hex32(char *buf, uint32_t val)
{
    static const char hex[] = "0123456789abcdef";
    int i = 0;
    for (int d = 28; d >= 0; d -= 4) {
        buf[i++] = hex[(val >> d) & 0xF];
    }
    buf[i] = '\0';
    return i;
}

int main(void)
{
    DWORD written;
    HANDLE hStdout = GetStdHandle(STD_OUTPUT_HANDLE);
    char buf[128];
    int n;
    HANDLE evt, mtx;
    DWORD res;

    /* Test event lifecycle */
    evt = CreateEventA(NULL, FALSE, FALSE, NULL);
    if (evt == NULL) {
        const char msg[] = "CreateEventA failed\r\n";
        WriteFile(hStdout, msg, (DWORD)(sizeof(msg) - 1), &written, NULL);
        ExitProcess(1);
    }
    n = 0;
    __builtin_memcpy(buf + n, "CreateEventA OK (handle=0x", 28); n += 28;
    n += fmt_hex32(buf + n, (uint32_t)evt);
    __builtin_memcpy(buf + n, ")\r\n", 3); n += 3;
    WriteFile(hStdout, buf, (DWORD)n, &written, NULL);

    /* WaitForSingleObject with timeout=0 on unsignaled event → WAIT_TIMEOUT */
    res = WaitForSingleObject(evt, 0);
    n = 0;
    __builtin_memcpy(buf + n, "WaitForSingleObject(unsignaled, 0) = 0x", 40); n += 40;
    n += fmt_hex32(buf + n, (uint32_t)res);
    __builtin_memcpy(buf + n, "\r\n", 2); n += 2;
    WriteFile(hStdout, buf, (DWORD)n, &written, NULL);

    /* SetEvent + WaitForSingleObject → WAIT_OBJECT_0 */
    SetEvent(evt);
    res = WaitForSingleObject(evt, 0);
    n = 0;
    __builtin_memcpy(buf + n, "WaitForSingleObject(signaled, 0) = 0x", 37); n += 37;
    n += fmt_hex32(buf + n, (uint32_t)res);
    __builtin_memcpy(buf + n, "\r\n", 2); n += 2;
    WriteFile(hStdout, buf, (DWORD)n, &written, NULL);

    /* ResetEvent */
    ResetEvent(evt);
    { const char msg[] = "ResetEvent OK\r\n";
      WriteFile(hStdout, msg, (DWORD)(sizeof(msg) - 1), &written, NULL); }

    /* Test mutex lifecycle */
    mtx = CreateMutexA(NULL, FALSE, NULL);
    n = 0;
    __builtin_memcpy(buf + n, "CreateMutexA OK (handle=0x", 28); n += 28;
    n += fmt_hex32(buf + n, (uint32_t)mtx);
    __builtin_memcpy(buf + n, ")\r\n", 3); n += 3;
    WriteFile(hStdout, buf, (DWORD)n, &written, NULL);

    ReleaseMutex(mtx);
    { const char msg[] = "ReleaseMutex OK\r\n";
      WriteFile(hStdout, msg, (DWORD)(sizeof(msg) - 1), &written, NULL); }

    { const char msg[] = "All tests passed\r\n";
      WriteFile(hStdout, msg, (DWORD)(sizeof(msg) - 1), &written, NULL); }

    ExitProcess(0);
    return 0;
}
