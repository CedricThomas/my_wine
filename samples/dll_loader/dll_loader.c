#include <stdio.h>
#include <windows.h>

int main(void)
{
    printf("=== DLL Loader Test ===\n");
    
    HMODULE hDll = LoadLibraryA("exportlib.dll");
    if (hDll == NULL) {
        printf("FAIL: LoadLibraryA(exportlib.dll) returned NULL\n");
        return 1;
    }
    printf("OK: LoadLibraryA -> %p\n", (void *)hDll);
    
    HMODULE hMod = GetModuleHandleA("exportlib.dll");
    if (hMod != hDll) {
        printf("FAIL: GetModuleHandleA mismatch\n");
        return 1;
    }
    printf("OK: GetModuleHandleA -> %p\n", (void *)hMod);
    
    typedef int (*dll_add_fn)(int, int);
    dll_add_fn add_fn = (dll_add_fn)GetProcAddress(hDll, "dll_add");
    if (add_fn == NULL) {
        printf("FAIL: GetProcAddress(dll_add) returned NULL\n");
        return 1;
    }
    int result = add_fn(3, 4);
    printf("OK: dll_add(3, 4) = %d\n", result);
    if (result != 7) {
        printf("FAIL: expected 7, got %d\n", result);
        return 1;
    }
    
    typedef const char *(*dll_greeting_fn)(void);
    dll_greeting_fn greet_fn = (dll_greeting_fn)GetProcAddress(hDll, "dll_greeting");
    if (greet_fn == NULL) {
        printf("FAIL: GetProcAddress(dll_greeting) returned NULL\n");
        return 1;
    }
    const char *msg = greet_fn();
    printf("OK: dll_greeting() = \"%s\"\n", msg);
    
    BOOL freed = FreeLibraryA(hDll);
    printf("OK: FreeLibraryA -> %d\n", freed);
    
    printf("\n=== ALL TESTS PASSED ===\n");
    return 0;
}
