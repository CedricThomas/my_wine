/*
 * entry_test_32.c — 32-bit my_wine sample: entry point & GetCommandLineA.
 *
 * Verifies that the PE32 entry point resolves correctly and that
 * GetCommandLineA returns a non-empty string (the PE path).
 *
 * Build: make samples SAMPLE=entry_test_32
 * Run:   make samples-run SAMPLE=entry_test_32
 */

#include <windows.h>

int main(void)
{
    HANDLE hStdout = GetStdHandle(STD_OUTPUT_HANDLE);
    DWORD written;
    LPCSTR cmdline = GetCommandLineA();
    char buf[4096];
    int len;

    if (!cmdline || cmdline[0] == '\0')
    {
        const char msg[] = "Entry test: FAIL - empty cmdline\r\n";
        WriteFile(hStdout, msg, (DWORD)(sizeof(msg) - 1), &written, NULL);
        ExitProcess(1);
    }

    len = (int)lstrlenA(cmdline);
    if (len > (int)sizeof(buf) - 25)
        len = (int)sizeof(buf) - 25;

    lstrcpyA(buf, "Entry test: cmdline=");
    lstrcatA(buf, cmdline);
    buf[20 + len] = '\r';
    buf[20 + len + 1] = '\n';
    buf[20 + len + 2] = '\0';

    WriteFile(hStdout, buf, 20 + len + 2, &written, NULL);

    const char ok[] = "Entry test: OK\r\n";
    WriteFile(hStdout, ok, (DWORD)(sizeof(ok) - 1), &written, NULL);

    ExitProcess(0);

    return 0;
}
