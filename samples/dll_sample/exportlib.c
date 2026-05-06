#include <windows.h>
#include <string.h>
#include <stdint.h>

__declspec(dllexport) int dll_add(int a, int b)
{
    return a + b;
}

__declspec(dllexport) const char *dll_greeting(void)
{
    return "Hello from exportlib.dll!";
}

__declspec(dllexport) DWORD dll_puts(const char *msg)
{
   HANDLE hStdout = GetStdHandle(STD_OUTPUT_HANDLE);
   DWORD written;

   WriteFile(hStdout, msg, (DWORD)(lstrlenA(msg)), &written, NULL);

   return 0;
}

__declspec(dllexport) int printmethod(const char *msg)
{
   HANDLE hStdout = GetStdHandle(STD_OUTPUT_HANDLE);
   DWORD written;

   WriteFile(hStdout, msg, (DWORD)(lstrlenA(msg)), &written, NULL);

   return 0;
}
