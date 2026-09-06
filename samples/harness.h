#ifndef MY_WINE_SAMPLES_HARNESS_H
#define MY_WINE_SAMPLES_HARNESS_H

#include <windows.h>

static void harness_signal(const char *event)
{
    HANDLE handle;
    DWORD written;
    const char prefix[] = "HARNESS:";
    const char suffix[] = "\r\n";

    if (event == NULL)
        return;

    handle = GetStdHandle(STD_OUTPUT_HANDLE);
    if (handle == NULL || handle == INVALID_HANDLE_VALUE)
        return;

    written = 0;
    WriteFile(handle, prefix, sizeof(prefix) - 1, &written, NULL);
    WriteFile(handle, event, lstrlenA(event), &written, NULL);
    WriteFile(handle, suffix, sizeof(suffix) - 1, &written, NULL);
}

#endif
