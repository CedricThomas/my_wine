/*
 * heap_test.c — Test heap management (HeapAlloc/HeapFree/HeapReAlloc/GetProcessHeap)
 *
 * Build: make samples SAMPLE=heap_test
 * Run:   ./my_wine samples/heap_test/heap_test.exe
 */

#include <windows.h>
#include <stdint.h>

static int fmt_hex64(char *buf, uint64_t val)
{
    static const char hex[] = "0123456789abcdef";
    int i = 0;
    for (int d = 60; d >= 0; d -= 4) {
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
    HANDLE heap;
    void *ptr, *reptr;
    uint8_t *wp;

    /* GetProcessHeap */
    heap = GetProcessHeap();
    if (heap == NULL) {
        const char msg[] = "GetProcessHeap failed\r\n";
        WriteFile(hStdout, msg, sizeof(msg)-1, &written, NULL);
        ExitProcess(1);
    }
    n = 0;
    __builtin_memcpy(buf + n, "GetProcessHeap OK (handle=0x", 31); n += 31;
    n += fmt_hex64(buf + n, (uint64_t)heap);
    __builtin_memcpy(buf + n, ")\r\n", 3); n += 3;
    WriteFile(hStdout, buf, (DWORD)n, &written, NULL);

    /* HeapAlloc 256 bytes */
    ptr = HeapAlloc(heap, 0, 256);
    if (ptr == NULL) {
        const char msg[] = "HeapAlloc(256) failed\r\n";
        WriteFile(hStdout, msg, sizeof(msg)-1, &written, NULL);
        ExitProcess(1);
    }
    /* Write a pattern into the allocation */
    wp = (uint8_t *)ptr;
    for (int i = 0; i < 256; i++) wp[i] = (uint8_t)(i & 0xFF);

    n = 0;
    __builtin_memcpy(buf + n, "HeapAlloc(256) OK (ptr=0x", 28); n += 28;
    n += fmt_hex64(buf + n, (uint64_t)ptr);
    __builtin_memcpy(buf + n, ")\r\n", 3); n += 3;
    WriteFile(hStdout, buf, (DWORD)n, &written, NULL);

    /* HeapReAlloc to 512 — verify old data preserved */
    reptr = HeapReAlloc(heap, 0, ptr, 512);
    if (reptr == NULL) {
        const char msg[] = "HeapReAlloc(512) failed\r\n";
        WriteFile(hStdout, msg, sizeof(msg)-1, &written, NULL);
        ExitProcess(1);
    }
    /* Check old data is preserved */
    wp = (uint8_t *)reptr;
    for (int i = 0; i < 256; i++) {
        if (wp[i] != (uint8_t)(i & 0xFF)) {
            const char msg[] = "HeapReAlloc data corruption!\r\n";
            WriteFile(hStdout, msg, sizeof(msg)-1, &written, NULL);
            ExitProcess(1);
        }
    }
    n = 0;
    __builtin_memcpy(buf + n, "HeapReAlloc(512) OK (ptr=0x", 30); n += 30;
    n += fmt_hex64(buf + n, (uint64_t)reptr);
    __builtin_memcpy(buf + n, "), data preserved\r\n", 19); n += 19;
    WriteFile(hStdout, buf, (DWORD)n, &written, NULL);

    /* HeapFree */
    if (!HeapFree(heap, 0, reptr)) {
        const char msg[] = "HeapFree failed\r\n";
        WriteFile(hStdout, msg, sizeof(msg)-1, &written, NULL);
        ExitProcess(1);
    }
    { const char msg[] = "HeapFree OK\r\n";
      WriteFile(hStdout, msg, sizeof(msg)-1, &written, NULL); }

    /* Multiple alloc/free cycle */
    for (int i = 0; i < 5; i++) {
        ptr = HeapAlloc(heap, 0, 128);
        if (ptr == NULL) {
            const char msg[] = "HeapAlloc cycle failed\r\n";
            WriteFile(hStdout, msg, sizeof(msg)-1, &written, NULL);
            ExitProcess(1);
        }
        HeapFree(heap, 0, ptr);
    }
    { const char msg[] = "Alloc/Free cycle OK\r\n";
      WriteFile(hStdout, msg, sizeof(msg)-1, &written, NULL); }

    /* HeapFree with NULL pointer (should be a no-op on Windows) */
    if (!HeapFree(heap, 0, NULL)) {
        const char msg[] = "HeapFree(NULL) failed\r\n";
        WriteFile(hStdout, msg, sizeof(msg)-1, &written, NULL);
        ExitProcess(1);
    }
    { const char msg[] = "HeapFree(NULL) OK\r\n";
      WriteFile(hStdout, msg, sizeof(msg)-1, &written, NULL); }

    { const char msg[] = "All tests passed\r\n";
      WriteFile(hStdout, msg, sizeof(msg)-1, &written, NULL); }

    ExitProcess(0);
    return 0;
}
