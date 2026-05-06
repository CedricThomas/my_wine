#include <stdio.h>
#include <string.h>

__declspec(dllexport) int dll_add(int a, int b)
{
    return a + b;
}

__declspec(dllexport) const char *dll_greeting(void)
{
    return "Hello from exportlib.dll!";
}
