#include <windows.h>

int main(void)
{
    HANDLE hStdout = GetStdHandle(STD_OUTPUT_HANDLE);
    const char msg[] = "Hello from my_wine!\r\n";
    DWORD written;

    WriteFile(hStdout, msg, (DWORD)sizeof(msg) - 1, &written, NULL);
    ExitProcess(0);

    return 0;
}
