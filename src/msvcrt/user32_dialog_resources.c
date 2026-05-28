/*
 * user32_dialog_resources.c
 *
 * Resource-backed dialog/UI utility exports split out from the core dialog
 * entrypoint file.
 */

#include <stdint.h>
#include <stdio.h>

#include "user32_dialog_priv.h"
#include "resource_win32.h"

KERNEL32_STUB
int LoadStringA(HINSTANCE hInstance, uint32_t uID, char *lpBuffer, int cchBufferMax)
{
    void *res;
    const uint16_t *table;
    uint32_t size = 0;
    uint32_t block_id = (uID / 16u) + 1u;
    uint32_t entry_id = uID % 16u;
    uint32_t i;
    uint16_t len;

    if (!lpBuffer || cchBufferMax <= 0)
        return 0;

    res = wine_resource_find((void *)(uintptr_t)hInstance, (const char *)(uintptr_t)6u,
                             (const char *)(uintptr_t)block_id, &size);
    if (!res)
        return 0;

    table = (const uint16_t *)wine_resource_lock(res);
    if (!table || size < sizeof(uint16_t))
        return 0;

    for (i = 0; i < entry_id; i++) {
        if ((const uint8_t *)table + sizeof(uint16_t) >
            (const uint8_t *)wine_resource_lock(res) + size)
            return 0;
        len = *table++;
        if ((const uint8_t *)(table + len) > (const uint8_t *)wine_resource_lock(res) + size)
            return 0;
        table += len;
    }

    len = *table++;
    if (len >= (uint16_t)cchBufferMax)
        len = (uint16_t)(cchBufferMax - 1);
    for (i = 0; i < len; i++) {
        uint16_t ch = table[i];
        lpBuffer[i] = (ch <= 0x7f) ? (char)ch : '?';
    }
    lpBuffer[len] = '\0';
    return (int)len;
}

KERNEL32_STUB
int MessageBoxA(HWND hWnd, const char *lpText, const char *lpCaption, UINT uType)
{
    (void)hWnd;
    (void)uType;
    fprintf(stderr, "MessageBoxA invoked: caption='%s' text='%s'\n",
            lpCaption ? lpCaption : "",
            lpText ? lpText : "");
    return IDOK;
}
