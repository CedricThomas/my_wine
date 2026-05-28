#include <stdint.h>

#include "user32_dialog_priv.h"
#include "resource_win32.h"
#include "include/common.h"

extern BOOL KERNEL32_ABI DestroyWindow(HWND hWnd);
extern HWND KERNEL32_ABI CreateWindowExA(DWORD dwExStyle, const char *lpClassName,
                                         const char *lpWindowName, DWORD dwStyle, int X, int Y,
                                         int nWidth, int nHeight, HWND hWndParent, HMENU hMenu,
                                         HINSTANCE hInstance, void *lpParam);

KERNEL32_STUB HWND GetDlgItem(HWND hDlg, int nIDDlgItem);
KERNEL32_STUB BOOL SetDlgItemTextA(HWND hDlg, int nIDDlgItem, const char *lpString);

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

BOOL user32_dialog_end(HWND hDlg, intptr_t nResult)
{
    DEBUG_LEVEL(1, "user32: EndDialog hwnd=0x%lx result=%ld",
                (unsigned long)(uintptr_t)hDlg, (long)nResult);
    user32_dialog_mark_modal_end(hDlg, nResult);
    if (hDlg != 0)
        DestroyWindow(hDlg);
    return TRUE;
}

int user32_dialog_run_modal(HWND hwnd, void *lpDialogFunc)
{
    intptr_t result = 0;
    int ended = 0;

    if (!user32_dialog_run_modal_lifecycle(hwnd, lpDialogFunc, &result, &ended))
        return 0;
    if (!ended)
        user32_dialog_end(hwnd, 0);
    else
        user32_dialog_mark_modal_end(hwnd, result);
    DEBUG_LEVEL(1, "user32: DialogBoxParamA hwnd=0x%lx result=%ld ended=%d",
                (unsigned long)(uintptr_t)hwnd, (long)result, ended);
    user32_dialog_clear_modal(hwnd);
    return (int)result;
}

KERNEL32_STUB
BOOL IsDialogMessageA(HWND hDlg, MSG *lpMsg)
{
    DLGPROC_WINE dlgproc;
    dialog_item_state *item;

    if (!hDlg || !lpMsg)
        return FALSE;

    dlgproc = user32_dialog_get_modal_dlgproc(hDlg);
    if (!dlgproc)
        return FALSE;

    if (lpMsg->hwnd != hDlg) {
        item = user32_dialog_find_item_by_handle(lpMsg->hwnd);
        if (!item || item->dialog != hDlg)
            return FALSE;
    }

    if (dlgproc(hDlg, lpMsg->message, lpMsg->wParam, lpMsg->lParam))
        return TRUE;
    return FALSE;
}

KERNEL32_STUB
HWND GetDlgItem(HWND hDlg, int nIDDlgItem)
{
    dialog_item_state *item = user32_dialog_find_item(hDlg, (uint32_t)nIDDlgItem, 1);
    if (!item)
        return 0;
    DEBUG_LEVEL(2, "user32: GetDlgItem dialog=0x%lx id=0x%x -> handle=0x%lx",
                (unsigned long)(uintptr_t)hDlg, (unsigned)nIDDlgItem,
                (unsigned long)(uintptr_t)item->handle);
    return item->handle;
}

KERNEL32_STUB
BOOL CheckDlgButton(HWND hDlg, int nIDButton, UINT uCheck)
{
    dialog_item_state *item = user32_dialog_find_item(hDlg, (uint32_t)nIDButton, 1);
    if (!item)
        return FALSE;
    item->state = uCheck;
    return TRUE;
}

KERNEL32_STUB
UINT IsDlgButtonChecked(HWND hDlg, int nIDButton)
{
    dialog_item_state *item = user32_dialog_find_item(hDlg, (uint32_t)nIDButton, 0);
    return item ? item->state : 0;
}

KERNEL32_STUB
BOOL SetDlgItemTextA(HWND hDlg, int nIDDlgItem, const char *lpString)
{
    dialog_item_state *item = user32_dialog_find_item(hDlg, (uint32_t)nIDDlgItem, 1);
    if (!item)
        return FALSE;
    if (!lpString)
        lpString = "";
    strncpy(item->text, lpString, sizeof(item->text) - 1);
    item->text[sizeof(item->text) - 1] = '\0';
    DEBUG_LEVEL(1, "user32: SetDlgItemTextA dialog=0x%lx id=0x%x text='%s'",
                (unsigned long)(uintptr_t)hDlg, (unsigned)nIDDlgItem, item->text);
    return TRUE;
}
