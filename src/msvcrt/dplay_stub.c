#include <stdint.h>

#include "kernel32_priv.h"

typedef BOOL (KERNEL32_ABI *DPENUM_CALLBACK_A)(const void *lpGuid, const char *lpName,
                                               uint32_t dwMajorVersion,
                                               uint32_t dwMinorVersion,
                                               void *lpContext);

typedef struct {
    uint32_t data1;
    uint16_t data2;
    uint16_t data3;
    uint8_t data4[8];
} dplay_guid;

static const dplay_guid g_dpspguid_ipx = {
    0x685bc400u, 0x9d2cu, 0x11cfu, { 0xa9u, 0xcdu, 0x00u, 0xaau, 0x00u, 0x68u, 0x86u, 0xe3u }
};

static const dplay_guid g_dpspguid_tcpip = {
    0x36e95ee0u, 0x8577u, 0x11cfu, { 0x96u, 0x0cu, 0x00u, 0x80u, 0xc7u, 0x53u, 0x4eu, 0x82u }
};

static const dplay_guid g_dpspguid_serial = {
    0x0f1d6860u, 0x88d9u, 0x11cfu, { 0x9cu, 0x4eu, 0x00u, 0xa0u, 0xc9u, 0x05u, 0x42u, 0x5eu }
};

static const dplay_guid g_dpspguid_modem = {
    0x44eaa760u, 0xcb68u, 0x11cfu, { 0x9cu, 0x4eu, 0x00u, 0xa0u, 0xc9u, 0x05u, 0x42u, 0x5eu }
};

KERNEL32_STUB
uint32_t DPCreate(void *lpGUID, void **lplpDP, void *pUnkOuter)
{
    (void)lpGUID;
    (void)pUnkOuter;
    if (lplpDP)
        *lplpDP = NULL;
    return 0x80004005u;
}

KERNEL32_STUB
uint32_t DirectPlayEnumerateA(DPENUM_CALLBACK_A callback, void *context)
{
    static const struct {
        const dplay_guid *guid;
        const char *name;
    } providers[] = {
        { &g_dpspguid_tcpip, "TCP/IP" },
        { &g_dpspguid_ipx, "IPX" },
        { &g_dpspguid_serial, "Serial" },
        { &g_dpspguid_modem, "Modem" },
    };
    uint32_t i;

    if (!callback)
        return 0;

    for (i = 0; i < sizeof(providers) / sizeof(providers[0]); i++) {
        if (!callback(providers[i].guid, providers[i].name, 6, 0, context))
            break;
    }

    return 0;
}
