/*
 * dll_loader.c — Test LoadLibraryA / GetProcAddress / GetModuleHandleA /
 *                FreeLibraryA using only kernel32 WriteFile + ExitProcess.
 *
 * Build:  ./samples/samples.sh build dll_loader
 * Run:    ./samples/samples.sh run dll_loader
 */

#include <windows.h>

static HANDLE hStdout;

/* Helper: write a string to stdout */
static void out(const char *s)
{
    DWORD w;
    WriteFile(hStdout, s, (DWORD)(lstrlenA(s)), &w, NULL);
}

/* Helper: print a pointer as "0xHHHHHHHH" */
static void out_ptr(HMODULE p)
{
    unsigned long long addr = (unsigned long long)p;
    char buf[12];
    int i;

    lstrcpyA(buf, "0x");
    for (i = 0; i < 8; i++) {
        int nibble = (int)((addr >> (28 - i * 4)) & 0xF);
        buf[2 + i] = (char)(nibble < 10 ? ('0' + nibble) : ('A' + nibble - 10));
    }
    buf[10] = '\0';
    out(buf);
}

/* Helper: print an int as decimal string */
static void out_int(int v)
{
    char buf[32];
    int i, tmp;

    if (v == 0) {
        out("0");
        return;
    }

    if (v < 0) {
        out("-");
        v = -v;
    }

    /* reverse into buffer */
    i = 0;
    tmp = v;
    while (tmp > 0) {
        buf[i++] = (char)('0' + (tmp % 10));
        tmp /= 10;
    }
    buf[i] = '\0';

    /* print in reverse */
    while (i > 0) {
        out(&buf[--i]);
    }
}

int main(void)
{
    char buf[128];
    HMODULE hDll, hMod;

    hStdout = GetStdHandle(STD_OUTPUT_HANDLE);

    /* 1. Banner */
    out("=== DLL Loader Test ===\r\n");

    /* 2. LoadLibraryA */
    hDll = LoadLibraryA("exportlib.dll");
    if (hDll == NULL) {
        out("FAIL: LoadLibraryA(exportlib.dll) returned NULL\r\n");
        ExitProcess(1);
    }
    lstrcpyA(buf, "OK: LoadLibraryA -> ");
    out(buf);
    out_ptr(hDll);
    out("\r\n");

    /* 3. GetModuleHandleA */
    hMod = GetModuleHandleA("exportlib.dll");
    if (hMod != hDll) {
        out("FAIL: GetModuleHandleA mismatch\r\n");
        ExitProcess(1);
    }
    lstrcpyA(buf, "OK: GetModuleHandleA -> ");
    out(buf);
    out_ptr(hMod);
    out("\r\n");

    /* 4. GetProcAddress("dll_add") + call */
    typedef int (*dll_add_fn)(int, int);
    dll_add_fn add_fn = (dll_add_fn)GetProcAddress(hDll, "dll_add");
    if (add_fn == NULL) {
        out("FAIL: GetProcAddress(dll_add) returned NULL\r\n");
        ExitProcess(1);
    }
    int result = add_fn(3, 4);

    lstrcpyA(buf, "OK: dll_add(3, 4) = ");
    out(buf);
    out_int(result);
    out("\r\n");

    if (result != 7) {
        lstrcpyA(buf, "FAIL: expected 7, got ");
        out(buf);
        out_int(result);
        out("\r\n");
        ExitProcess(1);
    }

    /* 5. GetProcAddress("dll_greeting") + call */
    typedef const char *(*dll_greeting_fn)(void);
    dll_greeting_fn greet_fn = (dll_greeting_fn)GetProcAddress(hDll, "dll_greeting");
    if (greet_fn == NULL) {
        out("FAIL: GetProcAddress(dll_greeting) returned NULL\r\n");
        ExitProcess(1);
    }
    const char *msg = greet_fn();

    lstrcpyA(buf, "OK: dll_greeting() = \"");
    out(buf);
    out(msg);
    out("\"\r\n");

    /* 5b. GetProcAddress("printmethod") + call */
    typedef int (*printmethod_fn)(const char *);
    printmethod_fn print_fn = (printmethod_fn)GetProcAddress(hDll, "printmethod");
    if (print_fn == NULL) {
        out("FAIL: GetProcAddress(printmethod) returned NULL\r\n");
        ExitProcess(1);
    }
    int rc = print_fn("Hello from printmethod!");
    if (rc != 0) {
        lstrcpyA(buf, "FAIL: printmethod returned non-zero\r\n");
        out(buf);
        ExitProcess(1);
    }
    out("OK: printmethod succeeded\r\n");

    /* 5c. GetProcAddress("dll_puts") + call */
    typedef DWORD (*dll_puts_fn)(const char *);
    dll_puts_fn puts_fn = (dll_puts_fn)GetProcAddress(hDll, "dll_puts");
    if (puts_fn == NULL) {
        out("FAIL: GetProcAddress(dll_puts) returned NULL\r\n");
        ExitProcess(1);
    }
    DWORD puts_rc = puts_fn("Test from dll_puts!");
    if (puts_rc != 0) {
        lstrcpyA(buf, "FAIL: dll_puts returned non-zero\r\n");
        out(buf);
        ExitProcess(1);
    }
    out("OK: dll_puts succeeded\r\n");

    /* 6. FreeLibrary */
    BOOL freed = FreeLibrary(hDll);

    lstrcpyA(buf, "OK: FreeLibrary -> ");
    out(buf);
    out_int(freed);
    out("\r\n");

    /* 7. Summary */
    out("\r\n=== ALL TESTS PASSED ===\r\n");

    ExitProcess(0);
}
