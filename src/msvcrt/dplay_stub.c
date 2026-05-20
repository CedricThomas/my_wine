#include <stdint.h>

#include "kernel32_priv.h"

KERNEL32_STUB
uint32_t DPCreate(void *lpGUID, void **lplpDP, void *pUnkOuter)
{
    (void)lpGUID;
    (void)pUnkOuter;
    if (lplpDP)
        *lplpDP = NULL;
    return 0x80004005u;
}
