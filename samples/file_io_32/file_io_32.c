/*
 * file_io_32.c — Test file I/O (PE32): CreateFileA, WriteFile, ReadFile, CloseHandle, DeleteFileA
 *
 * Build: make samples SAMPLE=file_io_32
 * Run:   ./my_wine samples/file_io_32/file_io_32.exe
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

#define TEST_PATH "C:\\tmp\\pi_test_file.dat"

int main(void)
{
    DWORD written, read;
    HANDLE hStdout = GetStdHandle(STD_OUTPUT_HANDLE);
    char buf[256];
    int n;
    HANDLE hFile;

    /* CreateFileA — create a new file */
    hFile = CreateFileA(TEST_PATH, GENERIC_READ | GENERIC_WRITE, 0, NULL,
                        CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile == INVALID_HANDLE_VALUE) {
        const char msg[] = "CreateFileA failed\r\n";
        WriteFile(hStdout, msg, sizeof(msg)-1, &written, NULL);
        ExitProcess(1);
    }
    n = 0;
    __builtin_memcpy(buf + n, "CreateFileA OK (handle=0x", 28); n += 28;
    n += fmt_hex32(buf + n, (uint32_t)hFile);
    __builtin_memcpy(buf + n, ")\r\n", 3); n += 3;
    WriteFile(hStdout, buf, (DWORD)n, &written, NULL);

    /* WriteFile — write test data */
    const char writeData[] = "Hello from Pi file I/O!";
    if (!WriteFile(hFile, writeData, sizeof(writeData)-1, &written, NULL)) {
        const char msg[] = "WriteFile failed\r\n";
        CloseHandle(hFile);
        WriteFile(hStdout, msg, sizeof(msg)-1, &written, NULL);
        ExitProcess(1);
    }
    n = 0;
    __builtin_memcpy(buf + n, "WriteFile OK (", 15); n += 15;
    /* Write decimal byte count */
    for (int d = 10000; d > 0; d /= 10) {
        if (n >= (int)sizeof(buf)-1) break;
        buf[n++] = '0' + (written / d) % 10;
    }
    __builtin_memcpy(buf + n, " bytes)\r\n", 9); n += 9;
    WriteFile(hStdout, buf, (DWORD)n, &written, NULL);

    /* Close and re-open for read */
    if (!CloseHandle(hFile)) {
        const char msg[] = "CloseHandle (after write) failed\r\n";
        WriteFile(hStdout, msg, sizeof(msg)-1, &written, NULL);
        ExitProcess(1);
    }
    { const char msg[] = "CloseHandle (after write) OK\r\n";
      WriteFile(hStdout, msg, sizeof(msg)-1, &written, NULL); }

    hFile = CreateFileA(TEST_PATH, GENERIC_READ, 0, NULL,
                        OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile == INVALID_HANDLE_VALUE) {
        const char msg[] = "CreateFileA (open for read) failed\r\n";
        WriteFile(hStdout, msg, sizeof(msg)-1, &written, NULL);
        ExitProcess(1);
    }

    /* ReadFile — read data back */
    char readBuf[64];
    if (!ReadFile(hFile, readBuf, sizeof(readBuf)-1, &read, NULL)) {
        const char msg[] = "ReadFile failed\r\n";
        CloseHandle(hFile);
        WriteFile(hStdout, msg, sizeof(msg)-1, &written, NULL);
        ExitProcess(1);
    }
    readBuf[read] = '\0';
    n = 0;
    __builtin_memcpy(buf + n, "ReadFile OK (", 13); n += 13;
    for (int d = 10000; d > 0; d /= 10) {
        if (n >= (int)sizeof(buf)-1) break;
        buf[n++] = '0' + (read / d) % 10;
    }
    __builtin_memcpy(buf + n, " bytes: \"", 8); n += 8;
    for (int i = 0; (DWORD)i < read && n < (int)sizeof(buf)-14; i++) {
        buf[n++] = readBuf[i];
    }
    __builtin_memcpy(buf + n, "\"\r\n", 3); n += 3;
    WriteFile(hStdout, buf, (DWORD)n, &written, NULL);

    /* Verify read data matches written data */
    if (read != sizeof(writeData)-1) {
        const char msg[] = "Read/Write size mismatch\r\n";
        CloseHandle(hFile);
        WriteFile(hStdout, msg, sizeof(msg)-1, &written, NULL);
        ExitProcess(1);
    }
    /* Use manual comparison — __builtin_memcmp can be mis-optimized
     * by mingw-w64 at -O2 when comparing a const local array against
     * another stack buffer in PE32 code. */
    {
        int mismatch = 0;
        for (int i = 0; (DWORD)i < read; i++) {
            if (readBuf[i] != writeData[i]) {
                mismatch = 1;
                break;
            }
        }
        if (mismatch) {
            const char msg[] = "Read/Write data mismatch\r\n";
            CloseHandle(hFile);
            WriteFile(hStdout, msg, sizeof(msg)-1, &written, NULL);
            ExitProcess(1);
        }
    }
    { const char msg[] = "Data verification OK\r\n";
      WriteFile(hStdout, msg, sizeof(msg)-1, &written, NULL); }

    /* Close handle */
    if (!CloseHandle(hFile)) {
        const char msg[] = "CloseHandle (after read) failed\r\n";
        WriteFile(hStdout, msg, sizeof(msg)-1, &written, NULL);
        ExitProcess(1);
    }
    { const char msg[] = "CloseHandle (after read) OK\r\n";
      WriteFile(hStdout, msg, sizeof(msg)-1, &written, NULL); }

    /* DeleteFileA — clean up */
    if (!DeleteFileA(TEST_PATH)) {
        const char msg[] = "DeleteFileA failed\r\n";
        WriteFile(hStdout, msg, sizeof(msg)-1, &written, NULL);
        ExitProcess(1);
    }
    { const char msg[] = "DeleteFileA OK\r\n";
      WriteFile(hStdout, msg, sizeof(msg)-1, &written, NULL); }

    { const char msg[] = "All tests passed\r\n";
      WriteFile(hStdout, msg, sizeof(msg)-1, &written, NULL); }

    ExitProcess(0);
    return 0;
}
