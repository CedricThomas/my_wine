#include "wine_native.h"

int wine_user_main(int argc, char *argv[])
{
    void *hStdout = GetStdHandle(STD_OUTPUT_HANDLE);
    const char msg[] = "Hello from my_wine!\r\n";
    uint32_t written;

    WriteFile(hStdout, msg, (uint32_t)sizeof(msg) - 1, &written, NULL);
    ExitProcess(0);

    return 0;
}
