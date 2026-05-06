#include <windows.h>
#include <string.h>
#include <stdint.h>

typedef uint32_t NTSTATUS;
__declspec(dllimport) NTSTATUS NtWriteFile(void *hFile, void *event, void *apc, void *context, const void *buffer, uint32_t length, uint32_t byteOffset, uint32_t *bytesWritten);

__declspec(dllexport) int dll_add(int a, int b)
{
    return a + b;
}

__declspec(dllexport) const char *dll_greeting(void)
{
    return "Hello from exportlib.dll!";
}

__declspec(dllexport) int printmethod(const char *msg)
{
    char buf[4096];
    uint32_t msg_len = (uint32_t)strlen(msg);
    uint32_t total_len = msg_len + 2; // \r\n

    if (total_len > sizeof(buf))
        total_len = (uint32_t)sizeof(buf);

    memcpy(buf, msg, msg_len);
    buf[msg_len] = '\r';
    buf[msg_len + 1] = '\n';

    HANDLE hStdout = GetStdHandle(STD_OUTPUT_HANDLE);
    uint32_t bytesWritten = 0;
    NtWriteFile(hStdout, NULL, NULL, NULL, buf, total_len, 0, &bytesWritten);

    return 0;
}
