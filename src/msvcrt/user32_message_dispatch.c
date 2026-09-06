/*
 * user32_message_dispatch.c
 *
 * Dispatch/send/default-proc responsibilities split out from the USER32
 * message-loop file.
 */

#include "user32_priv.h"
#include "include/debug.h"

KERNEL32_STUB LRESULT DefWindowProcA(HWND hWnd, UINT Msg, WPARAM wParam, LPARAM lParam);
KERNEL32_STUB LRESULT SendMessageA(HWND hWnd, UINT Msg, WPARAM wParam, LPARAM lParam);
KERNEL32_STUB BOOL DestroyWindow(HWND hwnd);
KERNEL32_STUB HCURSOR SetCursor(HCURSOR hCursor);
extern LRESULT user32_dialog_send_control_message(HWND hWnd, UINT Msg, WPARAM wParam,
                                                  LPARAM lParam) __attribute__((weak));

static void user32_fill_minmaxinfo(wine_window_entry *entry, MINMAXINFO *info)
{
    int display_w = 0;
    int display_h = 0;
    rb_rect_t rect;

    if (!info)
        return;

    user32_memset(info, 0, sizeof(*info));
    rb_display_get_size(&display_w, &display_h);

    info->ptMaxSize.x = display_w;
    info->ptMaxSize.y = display_h;
    info->ptMaxTrackSize.x = display_w;
    info->ptMaxTrackSize.y = display_h;
    info->ptMinTrackSize.x = 64;
    info->ptMinTrackSize.y = 64;

    if (entry && rb_window_get_client_rect(entry->sdl_window, &rect) == RB_OK) {
        if (rect.w > 0)
            info->ptMinTrackSize.x = rect.w;
        if (rect.h > 0)
            info->ptMinTrackSize.y = rect.h;
    }
}

static LRESULT user32_apply_class_cursor(HWND hWnd)
{
    wine_window_entry *entry = get_window_entry(hWnd);

    if (!entry)
        return FALSE;
    if (entry->class_cursor != 0) {
        SetCursor(entry->class_cursor);
        return TRUE;
    }
    return FALSE;
}

KERNEL32_STUB
LRESULT DispatchMessageA(const MSG *lpMsg)
{
    if (!lpMsg)
        return 0;

    if (lpMsg->message == WM_MOUSEMOVE ||
        lpMsg->message == WM_LBUTTONDOWN ||
        lpMsg->message == WM_LBUTTONUP ||
        lpMsg->message == WM_LBUTTONDBLCLK ||
        lpMsg->message == WM_RBUTTONDOWN ||
        lpMsg->message == WM_RBUTTONUP ||
        lpMsg->message == WM_RBUTTONDBLCLK ||
        lpMsg->message == WM_MBUTTONDOWN ||
        lpMsg->message == WM_MBUTTONUP ||
        lpMsg->message == WM_MBUTTONDBLCLK ||
        lpMsg->message == WM_MOUSEWHEEL) {
        SendMessageA(lpMsg->hwnd, WM_SETCURSOR, (WPARAM)lpMsg->hwnd, 0);
    }

    if (lpMsg->message == WM_CLOSE || lpMsg->message == WM_DESTROY ||
        lpMsg->message == WM_QUIT || lpMsg->message == WM_SYSCOMMAND) {
        DEBUG_WRITE_ERR("user32: DispatchMessageA close-path\n",
                        sizeof("user32: DispatchMessageA close-path\n") - 1);
    }

    {
        wine_window_entry *entry = get_window_entry(lpMsg->hwnd);
        if (entry && entry->wnd_proc) {
            return user32_call_wndproc((WNDPROC)entry->wnd_proc, lpMsg->hwnd,
                                       lpMsg->message, lpMsg->wParam, lpMsg->lParam);
        }
    }

    return DefWindowProcA(lpMsg->hwnd, lpMsg->message,
                          lpMsg->wParam, lpMsg->lParam);
}

KERNEL32_STUB
LRESULT SendMessageA(HWND hWnd, UINT Msg, WPARAM wParam, LPARAM lParam)
{
    wine_window_entry *entry = get_window_entry(hWnd);
    LRESULT dialog_result = (LRESULT)(intptr_t)-2147483647L;

    if (user32_dialog_send_control_message != NULL)
        dialog_result = user32_dialog_send_control_message(hWnd, Msg, wParam, lParam);

    if (dialog_result != (LRESULT)(intptr_t)-2147483647L)
        return dialog_result;

    switch (Msg) {
    case WM_GETTEXT: {
        char *buf = (char *)(intptr_t)lParam;
        int max = (int)wParam;

        if (!entry || max <= 0 || !buf)
            return 0;
        user32_strncpy(buf, entry->title, (size_t)max);
        buf[max - 1] = '\0';
        return (LRESULT)user32_strlen(buf);
    }

    case WM_SETTEXT: {
        const char *str = (const char *)(intptr_t)lParam;

        if (!entry)
            return 0;
        user32_strncpy(entry->title, str ? str : "", sizeof(entry->title) - 1);
        entry->title[sizeof(entry->title) - 1] = '\0';
        rb_window_set_title(entry->sdl_window, entry->title);
        return (LRESULT)TRUE;
    }

    case WM_GETMINMAXINFO: {
        MINMAXINFO *info = (MINMAXINFO *)(intptr_t)lParam;
        user32_fill_minmaxinfo(entry, info);
        return 0;
    }

    default:
        if (entry && entry->wnd_proc) {
            return user32_call_wndproc((WNDPROC)entry->wnd_proc, hWnd, Msg,
                                       wParam, lParam);
        }
        return DefWindowProcA(hWnd, Msg, wParam, lParam);
    }
}

KERNEL32_STUB
LRESULT DefWindowProcA(HWND hWnd, UINT Msg, WPARAM wParam, LPARAM lParam)
{
    const WPARAM sc_mask = 0xFFF0u;
    const WPARAM sc_close = 0xF060u;
    const LPARAM alt_context = (LPARAM)(1u << 29);

    if (Msg == WM_NCCREATE)
        return TRUE;
    if (Msg == WM_SETCURSOR)
        return user32_apply_class_cursor(hWnd);
    if (Msg == WM_CLOSE) {
        DEBUG_WRITE_ERR("user32: DefWindowProcA WM_CLOSE\n",
                        sizeof("user32: DefWindowProcA WM_CLOSE\n") - 1);
        DestroyWindow(hWnd);
        return 0;
    }
    if (Msg == WM_SYSCOMMAND && (wParam & sc_mask) == sc_close) {
        DEBUG_WRITE_ERR("user32: DefWindowProcA SC_CLOSE\n",
                        sizeof("user32: DefWindowProcA SC_CLOSE\n") - 1);
        DestroyWindow(hWnd);
        return 0;
    }
    if (Msg == WM_SYSKEYDOWN && wParam == VK_F4 &&
        (lParam & alt_context) != 0) {
        return DefWindowProcA(hWnd, WM_SYSCOMMAND, sc_close, lParam);
    }
    return 0;
}

KERNEL32_STUB
LRESULT CallWindowProcA(WNDPROC lpPrevWndFunc, HWND hWnd,
                        UINT Msg, WPARAM wParam, LPARAM lParam)
{
    return user32_call_wndproc(lpPrevWndFunc, hWnd, Msg, wParam, lParam);
}
