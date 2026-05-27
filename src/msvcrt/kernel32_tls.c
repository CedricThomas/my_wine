#include "kernel32_priv.h"

#define TLS_SLOTS_MAX 64

static uint32_t g_tls_bitmap = 0;
static void *g_tls_values[TLS_SLOTS_MAX];

KERNEL32_STUB
uint32_t TlsAlloc(void)
{
    uint32_t i;

    for (i = 0; i < TLS_SLOTS_MAX; i++) {
        uint32_t mask = 1u << i;
        if ((g_tls_bitmap & mask) == 0) {
            g_tls_bitmap |= mask;
            g_tls_values[i] = NULL;
            return i;
        }
    }
    return 0xffffffffu;
}

KERNEL32_STUB
int TlsFree(uint32_t dwTlsIndex)
{
    if (dwTlsIndex >= TLS_SLOTS_MAX)
        return 0;
    g_tls_bitmap &= ~(1u << dwTlsIndex);
    g_tls_values[dwTlsIndex] = NULL;
    return 1;
}

KERNEL32_STUB
int TlsSetValue(uint32_t dwTlsIndex, void *lpTlsValue)
{
    if (dwTlsIndex >= TLS_SLOTS_MAX || (g_tls_bitmap & (1u << dwTlsIndex)) == 0)
        return 0;
    g_tls_values[dwTlsIndex] = lpTlsValue;
    return 1;
}

KERNEL32_STUB
void *TlsGetValue(uint32_t dwTlsIndex)
{
    if (dwTlsIndex >= TLS_SLOTS_MAX || (g_tls_bitmap & (1u << dwTlsIndex)) == 0)
        return FORCE_PTR_RETURN(NULL);
    return FORCE_PTR_RETURN(g_tls_values[dwTlsIndex]);
}
