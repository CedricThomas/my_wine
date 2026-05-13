/*
 * multi_import_32.c — PE32 sample: multi-DLL import IAT resolution test.
 *
 * Imports from both kernel32.dll (GetStdHandle, WriteFile, ExitProcess)
 * and msvcrt.dll (malloc, free, strlen, memcpy) to verify that each
 * import descriptor's IAT is resolved independently.
 *
 * Build: make samples SAMPLE=multi_import_32
 * Run:   ./my_wine samples/multi_import_32/multi_import_32.exe
 */

#include <windows.h>
#include <stdlib.h>
#include <string.h>

int main(void)
{
    HANDLE hStdout = GetStdHandle(STD_OUTPUT_HANDLE);
    DWORD written;

    /* --- msvcrt.dll: malloc + memcpy + strlen --- */
    char *buf = (char *)malloc(256);
    if (!buf) {
        const char msg[] = "multi_import: FAIL - malloc\r\n";
        WriteFile(hStdout, msg, (DWORD)(sizeof(msg) - 1), &written, NULL);
        ExitProcess(1);
    }

    const char *payload = "Hello from multi-import!";
    memcpy(buf, payload, strlen(payload) + 1);

    const char prefix[] = "multi_import: msvcrt memcpy = ";
    DWORD pLen = (DWORD)sizeof(prefix) - 1;

    WriteFile(hStdout, prefix, pLen, &written, NULL);
    WriteFile(hStdout, buf, strlen(buf), &written, NULL);
    const char nl[] = "\r\n";
    WriteFile(hStdout, nl, 2, &written, NULL);

    /* --- msvcrt.dll: free --- */
    free(buf);

    /* --- kernel32.dll: WriteFile + ExitProcess --- */
    const char ok[] = "multi_import: OK (kernel32 + msvcrt)\r\n";
    WriteFile(hStdout, ok, (DWORD)(sizeof(ok) - 1), &written, NULL);

    ExitProcess(0);

    return 0;
}
