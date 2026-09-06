#include <string.h>

#include "kernel32_priv.h"

#define CP_ACP 0

typedef struct {
    uint16_t wProcessorArchitecture;
    uint16_t wReserved;
    uint32_t dwPageSize;
    void *lpMinimumApplicationAddress;
    void *lpMaximumApplicationAddress;
    uintptr_t dwActiveProcessorMask;
    uint32_t dwNumberOfProcessors;
    uint32_t dwProcessorType;
    uint32_t dwAllocationGranularity;
    uint16_t wProcessorLevel;
    uint16_t wProcessorRevision;
} SYSTEM_INFO_WINE;

typedef struct {
    uint32_t MaxCharSize;
    char DefaultChar[2];
    char LeadByte[12];
} CPINFO_WINE;

typedef struct {
    int32_t Bias;
    uint16_t StandardName[32];
    uint16_t StandardDate[8];
    int32_t StandardBias;
    uint16_t DaylightName[32];
    uint16_t DaylightDate[8];
    int32_t DaylightBias;
} TIME_ZONE_INFORMATION_WINE;

KERNEL32_STUB
uint32_t GetCurrentThreadId(void)
{
    return 1;
}

KERNEL32_STUB
uint32_t GetCurrentProcessId(void)
{
    return 1;
}

KERNEL32_STUB
void *GetCurrentThread(void)
{
    return FORCE_PTR_RETURN((void *)(uintptr_t)0xfffffffeu);
}

KERNEL32_STUB
void GetSystemInfo(SYSTEM_INFO_WINE *info)
{
    if (!info)
        return;
    memset(info, 0, sizeof(*info));
    info->dwPageSize = PAGE_SIZE;
    info->lpMinimumApplicationAddress = (void *)(uintptr_t)0x10000;
    info->lpMaximumApplicationAddress = (void *)(uintptr_t)0x7ffeffffu;
    info->dwActiveProcessorMask = 1;
    info->dwNumberOfProcessors = 1;
    info->dwProcessorType = 586;
    info->dwAllocationGranularity = 65536;
    info->wProcessorLevel = 5;
}

KERNEL32_STUB
uint32_t GetVersion(void)
{
    return 0x80000004u;
}

KERNEL32_STUB
uint32_t GetTimeZoneInformation(TIME_ZONE_INFORMATION_WINE *tz)
{
    if (!tz)
        return 0xffffffffu;
    memset(tz, 0, sizeof(*tz));
    return 0;
}

KERNEL32_STUB
uint32_t GetCPInfo(uint32_t code_page, CPINFO_WINE *info)
{
    if (code_page != CP_ACP && code_page != 1252)
        return 0;
    if (!info)
        return 0;
    memset(info, 0, sizeof(*info));
    info->MaxCharSize = 1;
    info->DefaultChar[0] = '?';
    return 1;
}

KERNEL32_STUB
char *GetEnvironmentStrings(void)
{
    return GetEnvironmentStringsA();
}
