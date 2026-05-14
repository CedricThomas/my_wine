/*
 * cmdline.c — my_wine sample: GetCommandLineA.
 *
 * Calls GetCommandLineA() and prints the returned command-line string.
 * Verifies the result is non-empty (should contain the PE path).
 *
 * Build: make samples SAMPLE=cmdline
 * Run:   make run-sample SAMPLE=cmdline
 */

#include <windows.h>

int main(void)
{
    HANDLE hStdout = GetStdHandle(STD_OUTPUT_HANDLE);
    const char *cmdline = GetCommandLineA();
    DWORD len;

    /* Verify we got a non-empty string */
    if (!cmdline || *cmdline == '\0')
    {
        const char err[] = "ERROR: GetCommandLineA returned empty or NULL\r\n";
        WriteFile(hStdout, err, sizeof(err) - 1, &len, NULL);
        ExitProcess(1);
    }

    const char prefix[] = "CmdLine: ";
    WriteFile(hStdout, prefix, sizeof(prefix) - 1, &len, NULL);
    WriteFile(hStdout, cmdline, (DWORD)lstrlenA(cmdline), &len, NULL);
    const char nl[] = "\r\n";
    WriteFile(hStdout, nl, sizeof(nl) - 1, &len, NULL);

    ExitProcess(0);

    return 0;
}
