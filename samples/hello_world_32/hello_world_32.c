/*
 * hello_world_32.c — Minimal 32-bit my_wine sample.
 *
 * Uses kernel32 WriteFile + ExitProcess (the two APIs my_wine supports
 * first). Build: ./build_samples.sh hello_world_32
 * Run:   ./build_samples.sh run hello_world_32
 */

#include <windows.h>

int main(void)
{
    HANDLE hStdout = GetStdHandle(STD_OUTPUT_HANDLE);
    const char msg[] = "Hello from 32-bit my_wine!\r\n";
    DWORD written;

    WriteFile(hStdout, msg, (DWORD)(sizeof(msg) - 1), &written, NULL);
    ExitProcess(0);

    return 0;
}
