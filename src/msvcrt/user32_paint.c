/*
 * user32_paint.c
 *
 * Paint/DC, coordinate-mapping, and window-metric helpers split out from the
 * main USER32 window lifecycle file.
 */

#include "user32_priv.h"

#define LPRECT  RECT*

KERNEL32_STUB BOOL AdjustWindowRectEx(RECT *lpRect, DWORD dwStyle, BOOL bMenu, DWORD dwExStyle);

KERNEL32_STUB
HDC BeginPaint(HWND hwnd, PAINTSTRUCT *lpPaint)
{
    wine_window_entry *entry = get_window_entry(hwnd);
    rb_dc_t dc;
    uint64_t handle;

    if (!entry || !lpPaint)
        return 0;

    user32_memset(lpPaint, 0, sizeof(*lpPaint));
    lpPaint->fErase = TRUE;
    lpPaint->fRestore = FALSE;
    lpPaint->fPaintValidateRect = TRUE;

    rb_window_get_client_rect(entry->sdl_window, &entry->client_rect);
    lpPaint->rcPaint.left = 0;
    lpPaint->rcPaint.top = 0;
    lpPaint->rcPaint.right = entry->client_rect.w;
    lpPaint->rcPaint.bottom = entry->client_rect.h;

    dc = rb_window_get_dc(entry->sdl_window);
    handle = wine_handle_alloc(HANDLE_TYPE_DC, (void *)(uintptr_t)dc);
    if (!handle) {
        rb_window_release_dc(entry->sdl_window, dc);
        return FORCE_HANDLE_RETURN(0, HDC);
    }

    lpPaint->hdc = (HDC)(uintptr_t)handle;
    return FORCE_HANDLE_RETURN(handle, HDC);
}

KERNEL32_STUB
BOOL EndPaint(HWND hwnd, const PAINTSTRUCT *lpPaint)
{
    wine_window_entry *entry;
    HDC hdc;
    rb_dc_t dc;

    if (!lpPaint)
        return FALSE;

    hdc = (HDC)(uintptr_t)lpPaint->hdc;
    if (!hdc)
        return TRUE;

    if (wine_handle_get_type((uint32_t)hdc) != HANDLE_TYPE_DC)
        return FALSE;

    entry = get_window_entry(hwnd);
    if (!entry)
        return FALSE;

    dc = (rb_dc_t)(uintptr_t)wine_handle_get((uint32_t)hdc);
    rb_window_release_dc(entry->sdl_window, dc);
    wine_handle_free((uint32_t)hdc);
    return TRUE;
}

KERNEL32_STUB
int MapWindowPoints(HWND hWndFrom, HWND hWndTo, POINT *lpPoints, UINT cPoints)
{
    RECT from_rect;
    RECT to_rect;
    int dx = 0;
    int dy = 0;
    UINT i;

    if (!lpPoints && cPoints != 0)
        return 0;

    if (hWndFrom && GetWindowRect(hWndFrom, &from_rect)) {
        dx += from_rect.left;
        dy += from_rect.top;
    }
    if (hWndTo && GetWindowRect(hWndTo, &to_rect)) {
        dx -= to_rect.left;
        dy -= to_rect.top;
    }

    for (i = 0; i < cPoints; i++) {
        lpPoints[i].x += dx;
        lpPoints[i].y += dy;
    }

    return (int)cPoints;
}

KERNEL32_STUB
int GetSystemMetrics(int nIndex)
{
    int w = 0;
    int h = 0;

    rb_display_get_size(&w, &h);
    switch (nIndex) {
    case SM_CXSCREEN:
    case SM_CXFULLSCREEN:
        return w;
    case SM_CYSCREEN:
    case SM_CYFULLSCREEN:
        return h;
    case SM_CXBORDER:
    case SM_CYBORDER:
        return 1;
    default:
        return 0;
    }
}

KERNEL32_STUB
BOOL AdjustWindowRect(LPRECT lpRect, DWORD dwStyle, BOOL bMenu)
{
    return AdjustWindowRectEx(lpRect, dwStyle, bMenu, 0);
}

static void user32_adjust_window_rect_impl(LPRECT lpRect, DWORD dwStyle,
                                           BOOL bMenu, DWORD dwExStyle)
{
    int left = 0;
    int right = 0;
    int top = 0;
    int bottom = 0;

    if (!lpRect)
        return;

    if (dwStyle & WS_CAPTION)
        top += 24;
    if (dwStyle & WS_THICKFRAME) {
        left += 4;
        right += 4;
        top += 4;
        bottom += 4;
    } else if (dwStyle & (WS_BORDER | WS_SYSMENU)) {
        left += 1;
        right += 1;
        top += 1;
        bottom += 1;
    }
    if (bMenu)
        top += 20;
    if (dwExStyle & WS_EX_WINDOWEDGE) {
        left += 2;
        right += 2;
        top += 2;
        bottom += 2;
    }

    lpRect->left -= left;
    lpRect->top -= top;
    lpRect->right += right;
    lpRect->bottom += bottom;
}

KERNEL32_STUB
BOOL AdjustWindowRectEx(LPRECT lpRect, DWORD dwStyle, BOOL bMenu, DWORD dwExStyle)
{
    user32_adjust_window_rect_impl(lpRect, dwStyle, bMenu, dwExStyle);
    return TRUE;
}

KERNEL32_STUB
HDC GetDC(HWND hwnd)
{
    wine_window_entry *entry = get_window_entry(hwnd);
    rb_dc_t dc;
    uint64_t handle;

    if (!entry)
        return FORCE_HANDLE_RETURN(0, HDC);

    dc = rb_window_get_dc(entry->sdl_window);
    handle = wine_handle_alloc(HANDLE_TYPE_DC, (void *)(uintptr_t)dc);
    if (!handle) {
        rb_window_release_dc(entry->sdl_window, dc);
        return FORCE_HANDLE_RETURN(0, HDC);
    }

    return FORCE_HANDLE_RETURN(handle, HDC);
}

KERNEL32_STUB
int ReleaseDC(HWND hwnd, HDC hdc)
{
    wine_window_entry *entry = get_window_entry(hwnd);
    rb_dc_t dc;

    if (!entry)
        return 0;

    if (wine_handle_get_type((uint32_t)hdc) != HANDLE_TYPE_DC)
        return 0;

    dc = (rb_dc_t)(uintptr_t)wine_handle_get((uint32_t)hdc);
    rb_window_release_dc(entry->sdl_window, dc);
    wine_handle_free((uint32_t)hdc);
    return 1;
}
