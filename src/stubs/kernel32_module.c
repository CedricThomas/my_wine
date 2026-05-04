#define _GNU_SOURCE

#include "kernel32_priv.h"

/* ── GetProcAddress ─────────────────────────────────────────── */

WINE_STUB
void *GetProcAddress(void *hModule, const char *lpProcName)
{
    (void)hModule;
    (void)lpProcName;
    return NULL;
}

/* ── LoadLibraryA ───────────────────────────────────────────── */

WINE_STUB
void *LoadLibraryA(const char *lpLibFileName)
{
    (void)lpLibFileName;
    return NULL;
}

/* ── GetModuleHandleA ───────────────────────────────────────── */

WINE_STUB
void *GetModuleHandleA(const char *lpModuleName)
{
    (void)lpModuleName;
    return NULL;
}
