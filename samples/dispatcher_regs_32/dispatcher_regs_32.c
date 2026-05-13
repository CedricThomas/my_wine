/*
 * dispatcher_regs_32.c — Verify register state preserved across syscall dispatch (PE32)
 *
 * Tests that the 32-bit dispatcher correctly saves and restores guest
 * registers across a syscall boundary by:
 *   1. Reading callee-saved registers (ESI, EDI, EBX) before
 *      a syscall
 *   2. Calling QueryPerformanceCounter (traverses thunk → dispatcher → back)
 *   3. Reading those registers again and comparing
 *
 * Also verifies EBP/frame pointer integrity by accessing a volatile
 * stack-local variable after the syscall (would crash or return garbage
 * if EBP was corrupted).
 *
 * Exit 0 on success, exit 1 on any register corruption detected.
 *
 * Build: make samples SAMPLE=dispatcher_regs_32
 * Run:   ./my_wine samples/dispatcher_regs_32/dispatcher_regs_32.exe
 */

#include <windows.h>
#include <stdio.h>
#include <stdint.h>

static int test_fail = 0;

static void write_msg(HANDLE h, const char *msg)
{
    DWORD n;
    while (*msg) {
        volatile int len = 0;
        while (msg[len] && msg[len] != '\n') len++;
        if (len == 0) { msg++; continue; }
        char tmp[512];
        __builtin_memcpy(tmp, msg, (size_t)len);
        tmp[len] = '\r';
        tmp[len + 1] = '\n';
        WriteFile(h, tmp, (DWORD)(len + 2), &n, NULL);
        msg += len + 1;
    }
}

static void hex_str(char *buf, uint32_t val)
{
    static const char hex[] = "0123456789abcdef";
    int i;
    for (i = 7; i >= 0; i--) {
        buf[7 - i] = hex[(val >> (i * 4)) & 0xF];
    }
    buf[8] = '\0';
}

/* Format a pass/fail line for one register check and write it. */
static void check_reg(const char *name, uint32_t before, uint32_t after,
                      HANDLE hStdout)
{
    char buf[128];
    int off = 0;
    if (before == after) {
        off = sprintf(buf, "PASS: %s preserved across syscall\n", name);
    } else {
        char hb[9], ha[9];
        hex_str(hb, before);
        hex_str(ha, after);
        off = sprintf(buf, "FAIL: %s changed (before=0x%s, after=0x%s)\n",
                      name, hb, ha);
        test_fail = 1;
    }
    buf[off] = '\0';
    write_msg(hStdout, buf);
}

int main(void)
{
    HANDLE hStdout = GetStdHandle(STD_OUTPUT_HANDLE);
    LARGE_INTEGER perf;
    volatile uint32_t frame_marker = 0xBADC0DE;

    write_msg(hStdout, "Dispatcher register preservation test (PE32)\n");

    uint32_t esi_before, edi_before, ebx_before;
    __asm__ volatile(
        "movl %%esi, %0\n"
        "movl %%edi, %1\n"
        "movl %%ebx, %2\n"
        : "=&c"(esi_before),
          "=&d"(edi_before), "=&D"(ebx_before)
    );

    /* The syscall: QPC → kernel32 stub → thunk → dispatcher. */
    QueryPerformanceCounter(&perf);
    /* Read regs after syscall. */
    uint32_t esi_after, edi_after, ebx_after;
    __asm__ volatile(
        "movl %%esi, %0\n"
        "movl %%edi, %1\n"
        "movl %%ebx, %2\n"
        : "=&c"(esi_after),
          "=&d"(edi_after), "=&D"(ebx_after)
    );

    check_reg("EBP", 0xBADC0DE, frame_marker, hStdout);
    check_reg("ESI", esi_before, esi_after, hStdout);
    check_reg("EDI", edi_before, edi_after, hStdout);
    check_reg("EBX", ebx_before, ebx_after, hStdout);
    write_msg(hStdout, test_fail ? "RESULT: FAILED\n" : "RESULT: ALL PASSED\n");
    ExitProcess(test_fail);
}
