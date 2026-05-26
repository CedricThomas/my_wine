#define _GNU_SOURCE

#include "kernel32_priv.h"

KERNEL32_STUB
void GetSystemTimeAsFileTime(FILETIME *lpSystemTime)
{
    if (lpSystemTime == NULL) {
        g_last_error = ERROR_INVALID_PARAMETER;
        return;
    }

    struct timespec ts;
    if (INLINE_SYSCALL_CLOCK_GETTIME(CLOCK_REALTIME, &ts) != 0) {
        return;
    }
    uint64_t filetime = (uint64_t)ts.tv_sec * 10000000ULL + (uint64_t)ts.tv_nsec / 100ULL;
    filetime += 116444736000000000ULL;

    lpSystemTime->dwLowDateTime = (uint32_t)(filetime & 0xFFFFFFFF);
    lpSystemTime->dwHighDateTime = (uint32_t)(filetime >> 32);
}

KERNEL32_STUB
int QueryPerformanceCounter(LARGE_INTEGER *lpPerformanceCount)
{
    if (lpPerformanceCount == NULL) {
        g_last_error = ERROR_INVALID_PARAMETER;
        return 0;
    }

    struct timespec ts;
    if (INLINE_SYSCALL_CLOCK_GETTIME(CLOCK_MONOTONIC, &ts) != 0) {
        return 0;
    }
    uint64_t counter = (uint64_t)ts.tv_sec * 10000000ULL + (uint64_t)ts.tv_nsec / 100ULL;
    lpPerformanceCount->QuadPart = (int64_t)counter;
    return 1;
}

KERNEL32_STUB
int QueryPerformanceFrequency(LARGE_INTEGER *lpFrequency)
{
    if (lpFrequency == NULL) {
        g_last_error = ERROR_INVALID_PARAMETER;
        return 0;
    }

    lpFrequency->QuadPart = (int64_t)10000000ULL;
    return 1;
}
