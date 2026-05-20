#include <stdint.h>

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

extern HWND CreateDialogParamA(HINSTANCE hInstance, const char *lpTemplateName, HWND hWndParent,
                               void *lpDialogFunc, LPARAM dwInitParam);
extern BOOL DestroyWindow(HWND hWnd);

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
    return hwnd ? IDOK : 0;
}

KERNEL32_STUB BOOL EndDialog(HWND hDlg, intptr_t nResult)
{
    (void)nResult;
    if (hDlg != 0)
        DestroyWindow(hDlg);
    return TRUE;
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
    (void)hDlg;
    (void)nIDDlgItem;
    if (lpString == NULL || cchMax <= 0)
        return 0;
    lpString[0] = '\0';
    return 0;
}

KERNEL32_STUB UINT GetDlgItemInt(HWND hDlg, int nIDDlgItem, BOOL *lpTranslated, BOOL bSigned)
{
    (void)hDlg;
    (void)nIDDlgItem;
    (void)bSigned;
    if (lpTranslated)
        *lpTranslated = FALSE;
    return 0;
}

KERNEL32_STUB BOOL SetDlgItemInt(HWND hDlg, int nIDDlgItem, UINT uValue, BOOL bSigned)
{
    (void)hDlg;
    (void)nIDDlgItem;
    (void)uValue;
    (void)bSigned;
    return TRUE;
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
