/*
 * time_test_32.c — Test time-related NT syscalls (PE32)
 *
 * Uses Windows API to exercise: GetSystemTimeAsFileTime,
 * QueryPerformanceCounter, QueryPerformanceFrequency, Sleep.
 * Build: make samples SAMPLE=time_test_32
 * Run:   ./my_wine samples/time_test_32/time_test_32.exe
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
    FILETIME ft;
    LARGE_INTEGER counter, frequency;
    char buf[128];
    int n;
    HANDLE hStdout;
    DWORD written;

    hStdout = GetStdHandle(STD_OUTPUT_HANDLE);

    /* Test GetSystemTimeAsFileTime */
    GetSystemTimeAsFileTime(&ft);
    n = 0;
    __builtin_memcpy(buf + n, "FileTime: low=0x", 17); n += 17;
    n += fmt_hex32(buf + n, (uint32_t)ft.dwLowDateTime);
    __builtin_memcpy(buf + n, " high=0x", 8); n += 8;
    n += fmt_hex32(buf + n, (uint32_t)ft.dwHighDateTime);
    __builtin_memcpy(buf + n, "\r\n", 2); n += 2;
    WriteFile(hStdout, buf, (DWORD)n, &written, NULL);

    /* Test QueryPerformanceCounter */
    QueryPerformanceCounter(&counter);
    n = 0;
    __builtin_memcpy(buf + n, "PerformanceCounter: 0x", 20); n += 20;
    n += fmt_hex32(buf + n, (uint32_t)counter.QuadPart);
    __builtin_memcpy(buf + n, "\r\n", 2); n += 2;
    WriteFile(hStdout, buf, (DWORD)n, &written, NULL);

    /* Test QueryPerformanceFrequency */
    QueryPerformanceFrequency(&frequency);
    n = 0;
    __builtin_memcpy(buf + n, "PerformanceFrequency: 0x", 22); n += 22;
    n += fmt_hex32(buf + n, (uint32_t)frequency.QuadPart);
    __builtin_memcpy(buf + n, "\r\n", 2); n += 2;
    WriteFile(hStdout, buf, (DWORD)n, &written, NULL);

    /* Test Sleep (maps to NtDelayExecution) */
    { const char msg[] = "Sleep(0) test...\r\n";
      WriteFile(hStdout, msg, (DWORD)(sizeof(msg) - 1), &written, NULL); }
    Sleep(0);
    { const char msg[] = "Done.\r\n";
      WriteFile(hStdout, msg, (DWORD)(sizeof(msg) - 1), &written, NULL); }

    ExitProcess(0);
    return 0;
}
