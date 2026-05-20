#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include "user32_priv.h"
#include "include/handle_manager.h"
#include "include/common.h"
#include "include/syscall_safe_utils.h"

#define IDOK 1

__attribute__((weak)) int g_user32_live_windows = 0;
__attribute__((weak)) int g_user32_window_create_attempted = 0;
__attribute__((weak)) HWND g_user32_active_window = 0;
__attribute__((weak)) HWND g_user32_focus_window = 0;

typedef struct {
    uint32_t length;
    uint32_t flags;
    uint32_t showCmd;
    int32_t ptMinPositionX;
    int32_t ptMinPositionY;
    int32_t ptMaxPositionX;
    int32_t ptMaxPositionY;
    int32_t rcNormalLeft;
    int32_t rcNormalTop;
    int32_t rcNormalRight;
    int32_t rcNormalBottom;
} WINDOWPLACEMENT_WINE;

extern HWND KERNEL32_ABI CreateDialogParamA(HINSTANCE hInstance, const char *lpTemplateName,
                                            HWND hWndParent, void *lpDialogFunc,
                                            LPARAM dwInitParam);
extern BOOL KERNEL32_ABI DestroyWindow(HWND hWnd);
extern HWND KERNEL32_ABI GetDlgItem(HWND hDlg, int nIDDlgItem);
extern LRESULT KERNEL32_ABI SendMessageA(HWND hWnd, UINT Msg, WPARAM wParam, LPARAM lParam);
extern LONG KERNEL32_ABI GetWindowLongA(HWND hwnd, int nIndex);

static int user32_strings_match(const char *a, const char *b)
{
    if (!a || !b)
        return 0;
    return user32_strcmp(a, b) == 0;
}

KERNEL32_STUB void InitCommonControls(void) {}

KERNEL32_STUB int PropertySheetA(void *header)
{
    (void)header;
    return 1;
}

KERNEL32_STUB void *GetSystemMenu(HWND hWnd, BOOL bRevert)
{
    (void)hWnd;
    (void)bRevert;
    return FORCE_PTR_RETURN((void *)(uintptr_t)1u);
}

KERNEL32_STUB BOOL AppendMenuA(void *hMenu, UINT uFlags, uintptr_t uIDNewItem, const char *lpNewItem)
{
    (void)hMenu;
    (void)uFlags;
    (void)uIDNewItem;
    (void)lpNewItem;
    return TRUE;
}

KERNEL32_STUB int DialogBoxParamA(HINSTANCE hInstance, const char *lpTemplateName, HWND hWndParent,
                                  void *lpDialogFunc, LPARAM dwInitParam)
{
    HWND hwnd = CreateDialogParamA(hInstance, lpTemplateName, hWndParent,
                                   lpDialogFunc, dwInitParam);
    DEBUG_LEVEL(1, "user32: DialogBoxParamA created hwnd=0x%lx template=%p dlgproc=%p",
                (unsigned long)(uintptr_t)hwnd, (const void *)lpTemplateName, lpDialogFunc);
    return user32_dialog_run_modal(hwnd, lpDialogFunc);
}

KERNEL32_STUB BOOL EndDialog(HWND hDlg, intptr_t nResult)
{
    return user32_dialog_end(hDlg, nResult);
}

KERNEL32_STUB BOOL PostThreadMessageA(uint32_t idThread, UINT Msg, WPARAM wParam, LPARAM lParam)
{
    (void)idThread;
    (void)Msg;
    (void)wParam;
    (void)lParam;
    return TRUE;
}

KERNEL32_STUB HWND FindWindowA(const char *lpClassName, const char *lpWindowName)
{
    uint32_t handle;

    for (handle = 1; handle <= HANDLE_TABLE_SIZE; handle++) {
        wine_window_entry *entry;

        if (wine_handle_get_type(handle) != HANDLE_TYPE_HWIN)
            continue;

        entry = (wine_window_entry *)wine_handle_get(handle);
        if (!entry)
            continue;
        if (lpClassName && !user32_strings_match(entry->class_name, lpClassName))
            continue;
        if (lpWindowName && !user32_strings_match(entry->title, lpWindowName))
            continue;
        return (HWND)(uintptr_t)handle;
    }

    return 0;
}

KERNEL32_STUB BOOL SetForegroundWindow(HWND hWnd)
{
    if (get_window_entry(hWnd) == NULL)
        return FALSE;
    user32_set_active_window(hWnd);
    user32_set_focus_window(hWnd);
    return TRUE;
}

KERNEL32_STUB BOOL IsIconic(HWND hWnd)
{
    return get_window_entry(hWnd) != NULL ? FALSE : FALSE;
}

KERNEL32_STUB BOOL IsWindowVisible(HWND hWnd)
{
    return get_window_entry(hWnd) != NULL ? TRUE : FALSE;
}

KERNEL32_STUB HWND GetLastActivePopup(HWND hWnd)
{
    if (get_window_entry(hWnd) != NULL)
        return hWnd;
    return user32_get_active_window();
}

KERNEL32_STUB HWND GetParent(HWND hWnd)
{
    wine_window_entry *entry = get_window_entry(hWnd);
    return entry ? entry->parent : 0;
}

KERNEL32_STUB UINT GetDlgItemTextA(HWND hDlg, int nIDDlgItem, char *lpString, int cchMax)
{
    HWND item = GetDlgItem(hDlg, nIDDlgItem);
    UINT copied;

    if (lpString == NULL || cchMax <= 0)
        return 0;

    copied = (UINT)SendMessageA(item, WM_GETTEXT, (WPARAM)cchMax, (LPARAM)(uintptr_t)lpString);
    if (copied >= (UINT)cchMax)
        copied = (UINT)(cchMax - 1);
    lpString[copied] = '\0';
    DEBUG_LEVEL(1, "user32: GetDlgItemTextA dialog=0x%lx id=0x%x -> '%s' (%u)",
                (unsigned long)(uintptr_t)hDlg, (unsigned)nIDDlgItem, lpString, copied);
    return copied;
}

KERNEL32_STUB UINT GetDlgItemInt(HWND hDlg, int nIDDlgItem, BOOL *lpTranslated, BOOL bSigned)
{
    HWND item = GetDlgItem(hDlg, nIDDlgItem);
    char text[64];
    char *end = NULL;
    long value;

    (void)bSigned;

    if (lpTranslated)
        *lpTranslated = FALSE;
    if (!item)
        return 0;

    text[0] = '\0';
    if (SendMessageA(item, WM_GETTEXT, (WPARAM)sizeof(text), (LPARAM)(uintptr_t)text) <= 0)
        return 0;

    value = strtol(text, &end, 10);
    if (end == text || *end != '\0' || value < 0)
        return 0;

    if (lpTranslated)
        *lpTranslated = TRUE;
    return (UINT)value;
}

KERNEL32_STUB BOOL SetDlgItemInt(HWND hDlg, int nIDDlgItem, UINT uValue, BOOL bSigned)
{
    HWND item = GetDlgItem(hDlg, nIDDlgItem);
    char text[32];

    (void)uValue;
    (void)bSigned;
    if (!item)
        return FALSE;

    if (bSigned)
        snprintf(text, sizeof(text), "%d", (int)uValue);
    else
        snprintf(text, sizeof(text), "%u", (unsigned)uValue);

    return SendMessageA(item, WM_SETTEXT, 0, (LPARAM)(uintptr_t)text) ? TRUE : FALSE;
}

KERNEL32_STUB BOOL WinHelpA(HWND hWndMain, const char *lpszHelp, UINT uCommand, uintptr_t dwData)
{
    (void)hWndMain;
    (void)lpszHelp;
    (void)uCommand;
    (void)dwData;
    return FALSE;
}

KERNEL32_STUB BOOL GetWindowPlacement(HWND hWnd, WINDOWPLACEMENT_WINE *lpwndpl)
{
    if (get_window_entry(hWnd) == NULL || lpwndpl == NULL)
        return FALSE;
    lpwndpl->showCmd = 1;
    return TRUE;
}

KERNEL32_STUB BOOL SetWindowPlacement(HWND hWnd, const WINDOWPLACEMENT_WINE *lpwndpl)
{
    (void)lpwndpl;
    return get_window_entry(hWnd) != NULL;
}

KERNEL32_STUB BOOL GetOpenFileNameA(void *info)
{
    (void)info;
    return FALSE;
}

KERNEL32_STUB BOOL GetSaveFileNameA(void *info)
{
    (void)info;
    return FALSE;
}

KERNEL32_STUB uint32_t CommDlgExtendedError(void)
{
    return 0;
}

KERNEL32_STUB int GetWindowTextA(HWND hWnd, char *lpString, int nMaxCount)
{
    if (lpString == NULL || nMaxCount <= 0)
        return 0;
    return (int)SendMessageA(hWnd, WM_GETTEXT, (WPARAM)nMaxCount, (LPARAM)(uintptr_t)lpString);
}

KERNEL32_STUB LONG GetDlgCtrlID(HWND hWnd)
{
    return GetWindowLongA(hWnd, GWL_ID);
}

KERNEL32_STUB LRESULT SendDlgItemMessageA(HWND hDlg, int nIDDlgItem, UINT Msg, WPARAM wParam, LPARAM lParam)
{
    return SendMessageA(GetDlgItem(hDlg, nIDDlgItem), Msg, wParam, lParam);
}

KERNEL32_STUB BOOL IsWindowEnabled(HWND hWnd)
{
    return get_window_entry(hWnd) != NULL;
}

KERNEL32_STUB UINT MapVirtualKeyA(UINT uCode, UINT uMapType)
{
    (void)uMapType;
    return uCode;
}

KERNEL32_STUB HWND CreateDialogIndirectParamA(HINSTANCE hInstance, const void *lpTemplate,
                                              HWND hWndParent, void *lpDialogFunc,
                                              LPARAM dwInitParam)
{
    (void)lpTemplate;
    return CreateDialogParamA(hInstance, (const char *)(uintptr_t)1u, hWndParent,
                              lpDialogFunc, dwInitParam);
}

KERNEL32_STUB LONG GetDialogBaseUnits(void)
{
    return (LONG)((8 & 0xffff) | ((16 & 0xffff) << 16));
}

KERNEL32_STUB BOOL CopyRect(RECT *lprcDst, const RECT *lprcSrc)
{
    if (lprcDst == NULL || lprcSrc == NULL)
        return FALSE;
    *lprcDst = *lprcSrc;
    return TRUE;
}
