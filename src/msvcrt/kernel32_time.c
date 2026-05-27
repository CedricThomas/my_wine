#define _GNU_SOURCE

#include "kernel32_priv.h"

KERNEL32_STUB
uint32_t GetTickCount(void)
{
    static uint32_t call_count = 0;
    struct timespec ts;
    uint32_t value;

    if (INLINE_SYSCALL_CLOCK_GETTIME(CLOCK_MONOTONIC, &ts) != 0)
        return 0;
    value = (uint32_t)((uint64_t)ts.tv_sec * 1000ULL + (uint64_t)ts.tv_nsec / 1000000ULL);

    if (debug_level_at_least(1)) {
        uint32_t count = ++call_count;
        if ((count & (count - 1)) == 0 || (count % 100000u) == 0)
            DEBUG("kernel32: GetTickCount count=%u value=%u", count, value);
    }

    return value;
}

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
int DosDateTimeToFileTime(uint16_t wFatDate, uint16_t wFatTime, FILETIME *lpFileTime)
{
    uint32_t dos_time;
    uint64_t value;

    if (!lpFileTime)
        return 0;

    dos_time = ((uint32_t)wFatDate << 16) | wFatTime;
    value = 116444736000000000ULL + (uint64_t)dos_time * 10000000ULL;
    lpFileTime->dwLowDateTime = (uint32_t)value;
    lpFileTime->dwHighDateTime = (uint32_t)(value >> 32);
    return 1;
}

KERNEL32_STUB
int FileTimeToDosDateTime(const FILETIME *lpFileTime, uint16_t *lpFatDate, uint16_t *lpFatTime)
{
    if (!lpFileTime || !lpFatDate || !lpFatTime)
        return 0;
    *lpFatDate = 0;
    *lpFatTime = 0;
    return 1;
}

KERNEL32_STUB
int FileTimeToLocalFileTime(const FILETIME *lpFileTime, FILETIME *lpLocalFileTime)
{
    if (!lpFileTime || !lpLocalFileTime)
        return 0;
    *lpLocalFileTime = *lpFileTime;
    return 1;
}

KERNEL32_STUB
int LocalFileTimeToFileTime(const FILETIME *lpLocalFileTime, FILETIME *lpFileTime)
{
    if (!lpLocalFileTime || !lpFileTime)
        return 0;
    *lpFileTime = *lpLocalFileTime;
    return 1;
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
