/*
 * time_test.c — Test time-related NT syscalls
 *
 * Uses Windows API to exercise: GetSystemTimeAsFileTime,
 * QueryPerformanceCounter, QueryPerformanceFrequency, Sleep.
 * Build: make samples SAMPLE=time_test
 * Run:   ./my_wine samples/time_test/time_test.exe
 */

#include <windows.h>
#include <stdio.h>

int main(void)
{
    FILETIME ft;
    LARGE_INTEGER counter, frequency;
    DWORD written;
    HANDLE hStdout;
    char buf[128];
    int n;

    hStdout = GetStdHandle(STD_OUTPUT_HANDLE);

    /* Test GetSystemTimeAsFileTime */
    GetSystemTimeAsFileTime(&ft);
    n = sprintf(buf, "FileTime: low=0x%lx high=0x%lx\r\n",
                ft.dwLowDateTime, ft.dwHighDateTime);
    WriteFile(hStdout, buf, (DWORD)n, &written, NULL);

    /* Test QueryPerformanceCounter */
    QueryPerformanceCounter(&counter);
    n = sprintf(buf, "PerformanceCounter: 0x%llx\r\n",
                (unsigned long long)counter.QuadPart);
    WriteFile(hStdout, buf, (DWORD)n, &written, NULL);

    /* Test QueryPerformanceFrequency */
    QueryPerformanceFrequency(&frequency);
    n = sprintf(buf, "PerformanceFrequency: %lld\r\n",
                (long long)frequency.QuadPart);
    WriteFile(hStdout, buf, (DWORD)n, &written, NULL);

    /* Test Sleep (maps to NtDelayExecution) */
    n = sprintf(buf, "Sleep(0) test...\r\n");
    WriteFile(hStdout, buf, (DWORD)n, &written, NULL);
    Sleep(0);
    n = sprintf(buf, "Done.\r\n");
    WriteFile(hStdout, buf, (DWORD)n, &written, NULL);

    ExitProcess(0);
    return 0;
}
