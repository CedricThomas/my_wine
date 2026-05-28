/*
 * user32_dialog_create.c
 *
 * Dialog creation and resource-backed initialization split out from the
 * broader dialog entrypoint cluster.
 */

#include <stdint.h>

#include "user32_dialog_priv.h"
#include "resource_win32.h"
#include "include/common.h"

extern HWND KERNEL32_ABI CreateWindowExA(DWORD dwExStyle, const char *lpClassName,
                                         const char *lpWindowName, DWORD dwStyle, int X, int Y,
                                         int nWidth, int nHeight, HWND hWndParent, HMENU hMenu,
                                         HINSTANCE hInstance, void *lpParam);

KERNEL32_STUB
HWND CreateDialogParamA(HINSTANCE hInstance, const char *lpTemplateName, HWND hWndParent,
                        void *lpDialogFunc, LPARAM dwInitParam)
{
    HWND hwnd;
    (void)hInstance;

    DEBUG_LEVEL(1, "user32: CreateDialogParamA hInstance=0x%lx template=%p dlgproc=%p",
                (unsigned long)(uintptr_t)hInstance, (const void *)lpTemplateName, lpDialogFunc);

    if (!wine_resource_find((void *)(uintptr_t)hInstance, (const char *)(uintptr_t)5u,
                            lpTemplateName, NULL)) {
        DEBUG_LEVEL(1, "user32: CreateDialogParamA resource lookup failed");
        return 0;
    }

    user32_dialog_ensure_class();
    hwnd = CreateWindowExA(0, "MY_WINE_DIALOG", "Dialog", WS_POPUP | WS_CAPTION,
                           0, 0, 320, 200, hWndParent, 0, hInstance, NULL);
    if (hwnd)
        user32_dialog_set_modal_dlgproc(hwnd, lpDialogFunc);
    if (hwnd && lpDialogFunc) {
        DEBUG_LEVEL(1, "user32: CreateDialogParamA init hwnd=0x%lx",
                    (unsigned long)(uintptr_t)hwnd);
        ((DLGPROC_WINE)lpDialogFunc)(hwnd, WM_INITDIALOG, 0, dwInitParam);
        user32_dialog_try_doom95_autostart(hInstance, hwnd, lpTemplateName, lpDialogFunc);
    }
    return hwnd;
}
