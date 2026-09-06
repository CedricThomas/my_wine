/*
 * virtual_mem_32.c — Test virtual memory management (PE32)
 *                   (VirtualAlloc / VirtualFree / VirtualProtect)
 *
 * Build: make samples SAMPLE=virtual_mem_32
 * Run:   ./my_wine samples/virtual_mem_32/virtual_mem_32.exe
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
    void *ptr;
    uint8_t *wp;
    DWORD oldProtect;

    /* VirtualAlloc MEM_COMMIT | PAGE_READWRITE */
    ptr = VirtualAlloc(NULL, 4096, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (ptr == NULL) {
        const char msg[] = "VirtualAlloc failed\r\n";
        WriteFile(hStdout, msg, sizeof(msg)-1, &written, NULL);
        ExitProcess(1);
    }
    n = 0;
    __builtin_memcpy(buf + n, "VirtualAlloc OK (ptr=0x", 24); n += 24;
    n += fmt_hex32(buf + n, (uint32_t)ptr);
    __builtin_memcpy(buf + n, ")\r\n", 3); n += 3;
    WriteFile(hStdout, buf, (DWORD)n, &written, NULL);

    /* Write a pattern into the allocated region */
    wp = (uint8_t *)ptr;
    for (int i = 0; i < 4096; i++) wp[i] = (uint8_t)(i & 0xFF);
    { const char msg[] = "Write to RW page OK\r\n";
      WriteFile(hStdout, msg, sizeof(msg)-1, &written, NULL); }

    /* Read it back and verify */
    for (int i = 0; i < 4096; i++) {
        if (wp[i] != (uint8_t)(i & 0xFF)) {
            const char msg[] = "Read verification failed!\r\n";
            WriteFile(hStdout, msg, sizeof(msg)-1, &written, NULL);
            ExitProcess(1);
        }
    }
    { const char msg[] = "Read verification OK\r\n";
      WriteFile(hStdout, msg, sizeof(msg)-1, &written, NULL); }

    /* VirtualProtect to PAGE_READONLY */
    if (!VirtualProtect(ptr, 4096, PAGE_READONLY, &oldProtect)) {
        const char msg[] = "VirtualProtect to PAGE_READONLY failed\r\n";
        WriteFile(hStdout, msg, sizeof(msg)-1, &written, NULL);
        ExitProcess(1);
    }
    { const char msg[] = "VirtualProtect -> PAGE_READONLY OK\r\n";
      WriteFile(hStdout, msg, sizeof(msg)-1, &written, NULL); }

    /* Verify data is still readable */
    for (int i = 0; i < 4096; i++) {
        if (wp[i] != (uint8_t)(i & 0xFF)) {
            const char msg[] = "RO read verification failed!\r\n";
            WriteFile(hStdout, msg, sizeof(msg)-1, &written, NULL);
            ExitProcess(1);
        }
    }
    { const char msg[] = "RO read verification OK\r\n";
      WriteFile(hStdout, msg, sizeof(msg)-1, &written, NULL); }

    /* Attempt write to readonly page — should fail (access violation) */
    /* We can't easily trap the exception in a minimal PE32 sample without
       SEH support, so we use a try/except block if available, or just
       VirtualProtect back and note the expectation. Instead, we skip the
       actual write and just verify VirtualProtect back to RW works. */
    { const char msg[] = "Write to RO page expected to fail (skipped — no SEH)\r\n";
      WriteFile(hStdout, msg, sizeof(msg)-1, &written, NULL); }

    /* VirtualProtect back to PAGE_READWRITE */
    if (!VirtualProtect(ptr, 4096, PAGE_READWRITE, &oldProtect)) {
        const char msg[] = "VirtualProtect back to PAGE_READWRITE failed\r\n";
        WriteFile(hStdout, msg, sizeof(msg)-1, &written, NULL);
        ExitProcess(1);
    }
    { const char msg[] = "VirtualProtect -> PAGE_READWRITE OK\r\n";
      WriteFile(hStdout, msg, sizeof(msg)-1, &written, NULL); }

    /* Write again to confirm RW restored */
    wp[0] = 0xAA;
    if (wp[0] != 0xAA) {
        const char msg[] = "Post-RW write verification failed!\r\n";
        WriteFile(hStdout, msg, sizeof(msg)-1, &written, NULL);
        ExitProcess(1);
    }
    { const char msg[] = "Post-RW write OK\r\n";
      WriteFile(hStdout, msg, sizeof(msg)-1, &written, NULL); }

    /* VirtualFree MEM_RELEASE */
    if (!VirtualFree(ptr, 0, MEM_RELEASE)) {
        const char msg[] = "VirtualFree failed\r\n";
        WriteFile(hStdout, msg, sizeof(msg)-1, &written, NULL);
        ExitProcess(1);
    }
    { const char msg[] = "VirtualFree OK\r\n";
      WriteFile(hStdout, msg, sizeof(msg)-1, &written, NULL); }

    { const char msg[] = "All tests passed\r\n";
      WriteFile(hStdout, msg, sizeof(msg)-1, &written, NULL); }

    ExitProcess(0);
    return 0;
}
