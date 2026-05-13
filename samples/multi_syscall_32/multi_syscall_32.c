/*
 * multi_syscall_32.c — Test multiple NT syscalls in sequence (PE32)
 *
 * Uses kernel32 WriteFile + ExitProcess via Windows API.
 * Tests: GetStdHandle, multiple WriteFile calls, ExitProcess.
 * Build: make samples SAMPLE=multi_syscall_32
 * Run:   ./my_wine samples/multi_syscall_32/multi_syscall_32.exe
 */

#include <windows.h>

int main(void)
{
    HANDLE hStdout = GetStdHandle(STD_OUTPUT_HANDLE);
    const char msg1[] = "Hello from multi_syscall!\r\n";
    const char msg2[] = "Test complete.\r\n";
    DWORD written;

    WriteFile(hStdout, msg1, (DWORD)(sizeof(msg1) - 1), &written, NULL);
    WriteFile(hStdout, msg2, (DWORD)(sizeof(msg2) - 1), &written, NULL);
    ExitProcess(0);

    return 0;
}
