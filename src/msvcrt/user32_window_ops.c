/*
 * user32_window_ops.c
 *
 * Geometry, visibility, and simple window-state APIs split out from the main
 * USER32 window lifecycle file.
 */

#include "user32_priv.h"

#define SWP_NOSIZE           0x0001
#define SWP_NOMOVE           0x0002
#define SWP_NOACTIVATE       0x0010
#define SWP_SHOWWINDOW       0x0040
#define SWP_HIDEWINDOW       0x0080
#define USER32_DESKTOP_HWND  ((HWND)(uintptr_t)0x7fffff00u)

KERNEL32_STUB
BOOL ShowWindow(HWND hwnd, int nCmdShow)
{
    wine_window_entry *entry = get_window_entry(hwnd);
    if (!entry)
        return FALSE;

    switch (nCmdShow) {
    case SW_HIDE:
        rb_window_show(entry->sdl_window, 0);
        return TRUE;
    case SW_SHOWNORMAL:
    case SW_SHOW:
    case SW_SHOWNA:
    case SW_SHOWDEFAULT:
        rb_window_show(entry->sdl_window, 1);
        user32_set_foreground_focus(hwnd, 1);
        return TRUE;
    case SW_RESTORE:
        rb_window_restore(entry->sdl_window);
        user32_set_foreground_focus(hwnd, 1);
        return TRUE;
    case SW_SHOWMINIMIZED:
    case SW_MINIMIZE:
        rb_window_minimize(entry->sdl_window);
        return TRUE;
    case SW_SHOWMAXIMIZED:
        rb_window_maximize(entry->sdl_window);
        user32_set_foreground_focus(hwnd, 1);
        return TRUE;
    default:
        rb_window_show(entry->sdl_window, 1);
        user32_set_foreground_focus(hwnd, 1);
        return TRUE;
    }
}

KERNEL32_STUB
BOOL SetWindowPos(HWND hwnd, HWND hWndInsertAfter,
                  int x, int y, int cx, int cy, UINT uFlags)
{
    wine_window_entry *entry = get_window_entry(hwnd);
    (void)hWndInsertAfter;
    if (!entry)
        return FALSE;

    if (!(uFlags & SWP_NOMOVE))
        rb_window_set_position(entry->sdl_window, x, y);
    if (!(uFlags & SWP_NOSIZE))
        rb_window_set_size(entry->sdl_window, cx, cy);
    if (uFlags & SWP_HIDEWINDOW)
        rb_window_show(entry->sdl_window, 0);
    if (uFlags & SWP_SHOWWINDOW)
        rb_window_show(entry->sdl_window, 1);
    if (!(uFlags & SWP_NOACTIVATE) && !(uFlags & SWP_HIDEWINDOW))
        user32_set_foreground_focus(hwnd, 1);
    return TRUE;
}

KERNEL32_STUB
BOOL MoveWindow(HWND hwnd, int x, int y, int nWidth, int nHeight, BOOL bRepaint)
{
    wine_window_entry *entry = get_window_entry(hwnd);
    (void)bRepaint;
    if (!entry)
        return FALSE;

    rb_window_set_position(entry->sdl_window, x, y);
    rb_window_set_size(entry->sdl_window, nWidth, nHeight);
    return TRUE;
}

KERNEL32_STUB
BOOL SetWindowTextA(HWND hwnd, const char *lpString)
{
    wine_window_entry *entry = get_window_entry(hwnd);
    if (!entry)
        return FALSE;

    user32_strncpy(entry->title, lpString ? lpString : "", sizeof(entry->title) - 1);
    entry->title[sizeof(entry->title) - 1] = '\0';
    rb_window_set_title(entry->sdl_window, entry->title);
    return TRUE;
}

KERNEL32_STUB
BOOL GetWindowRect(HWND hwnd, RECT *lpRect)
{
    wine_window_entry *entry = get_window_entry(hwnd);
    rb_rect_t r;

    if (!entry || !lpRect)
        return FALSE;
    if (rb_window_get_rect(entry->sdl_window, &r) != RB_OK)
        return FALSE;

    lpRect->left = r.x;
    lpRect->top = r.y;
    lpRect->right = r.x + r.w;
    lpRect->bottom = r.y + r.h;
    return TRUE;
}

KERNEL32_STUB
BOOL GetClientRect(HWND hwnd, RECT *lpRect)
{
    wine_window_entry *entry = get_window_entry(hwnd);
    rb_rect_t r;

    if (!entry || !lpRect)
        return FALSE;
    if (rb_window_get_client_rect(entry->sdl_window, &r) != RB_OK)
        return FALSE;

    lpRect->left = 0;
    lpRect->top = 0;
    lpRect->right = r.w;
    lpRect->bottom = r.h;
    return TRUE;
}

KERNEL32_STUB
BOOL IsWindow(HWND hwnd)
{
    if (!hwnd)
        return FALSE;
    return wine_handle_get_type((uint32_t)hwnd) == HANDLE_TYPE_HWIN;
}

KERNEL32_STUB
BOOL EnableWindow(HWND hwnd, BOOL bEnable)
{
    (void)hwnd;
    (void)bEnable;
    return TRUE;
}

KERNEL32_STUB
HWND GetDesktopWindow(void)
{
    return FORCE_HANDLE_RETURN(USER32_DESKTOP_HWND, HWND);
}

KERNEL32_STUB
BOOL UpdateWindow(HWND hwnd)
{
    (void)hwnd;
    return TRUE;
}

KERNEL32_STUB
BOOL InvalidateRect(HWND hwnd, const RECT *lpRect, BOOL bErase)
{
    (void)hwnd;
    (void)lpRect;
    (void)bErase;
    return TRUE;
}

KERNEL32_STUB
BOOL ValidateRect(HWND hwnd, const RECT *lpRect)
{
    (void)hwnd;
    (void)lpRect;
    return TRUE;
}
