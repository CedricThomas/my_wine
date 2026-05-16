/*
 * user32_message.c
 *
 * 13 message-loop and message-related stub functions for user32.dll.
 * Implements: GetMessageA, PeekMessageA, TranslateMessage, DispatchMessageA,
 * PostMessageA, PostQuitMessage, SendMessageA, DefWindowProcA, CallWindowProcA,
 * SetWindowsHookExA, UnhookWindowsHookEx, CallNextHookEx, SystemParametersInfoA.
 *
 * All exported functions use KERNEL32_STUB (stdcall on i386, ms_abi on x86_64)
 * to match the calling convention of guest PE binaries.
 *
 * NOTE: This file is excluded from the default build. It depends on backend
 * symbols (rb_event_wait, rb_event_peek, rb_event_push, etc.) that are only
 * available when the SDL2 backend is linked.
 */

#include <string.h>
#include <stdint.h>

#include "user32_priv.h"
#include "../include/render_backend.h"

/* Forward declarations for cross-referenced stubs within this file */
KERNEL32_STUB LRESULT DefWindowProcA(HWND hWnd, UINT Msg, WPARAM wParam, LPARAM lParam);
KERNEL32_STUB BOOL DestroyWindow(HWND hwnd);

static int g_quit_pending = 0;
static int g_quit_exit_code = 0;

/* ── Helper: copy an rb_msg_t into an MSG ──────────────────── */
static void copy_rb_msg_to_MSG(const rb_msg_t *src, MSG *dst)
{
    dst->hwnd    = src->hwnd;
    dst->message = src->message;
    dst->wParam  = src->wParam;
    dst->lParam  = src->lParam;
    dst->time    = src->time;
    dst->pt.x    = src->pt_x;
    dst->pt.y    = src->pt_y;
}

/* ═══════════════════════════════════════════════════════════
 * 13 exported message functions
 * ═══════════════════════════════════════════════════════════ */

/* ── 1. GetMessageA ────────────────────────────────────────── */
/*
 * Blocking call: waits for a message via rb_event_wait.
 * Copies rb_msg_t fields to MSG (explicit pt.x = pt_x, pt.y = pt_y).
 * Returns 0 on WM_QUIT (matching Windows behavior).
 * Returns 1 for all other messages.
 * Sets *wRemoveMsg is ignored; we always remove (pop) the message.
 */
KERNEL32_STUB
BOOL GetMessageA(MSG *lpMsg, HWND hWnd, UINT wMsgFilterMin, UINT wMsgFilterMax)
{
    (void)hWnd;
    (void)wMsgFilterMin;
    (void)wMsgFilterMax;

    if (!lpMsg)
        return FALSE;

    if (g_user32_window_create_attempted && g_user32_live_windows == 0) {
        memset(lpMsg, 0, sizeof(*lpMsg));
        lpMsg->message = WM_QUIT;
        return 0;
    }

    if (g_quit_pending) {
        g_quit_pending = 0;
        memset(lpMsg, 0, sizeof(*lpMsg));
        lpMsg->message = WM_QUIT;
        lpMsg->wParam = (WPARAM)g_quit_exit_code;
        return 0;
    }

    rb_msg_t rb;
    int ret = rb_event_wait(&rb);
    if (ret <= 0)
        return 0;

    copy_rb_msg_to_MSG(&rb, lpMsg);

    if (lpMsg->message == WM_QUIT)
        return 0;
    return 1;
}

/* ── 2. PeekMessageA ──────────────────────────────────────── */
/*
 * Non-blocking call: checks for a message via rb_event_peek.
 * Copies rb_msg_t fields to MSG (explicit pt.x = pt_x, pt.y = pt_y).
 * Returns 1 if a message was available, 0 if queue is empty.
 * PM_REMOVE (1) pops; PM_NOREMOVE (0) peeks without removing.
 */
KERNEL32_STUB
BOOL PeekMessageA(MSG *lpMsg, HWND hWnd, UINT wMsgFilterMin,
                  UINT wMsgFilterMax, UINT wRemoveMsg)
{
    (void)hWnd;
    (void)wMsgFilterMin;
    (void)wMsgFilterMax;
    (void)wRemoveMsg;

    if (!lpMsg)
        return FALSE;

    rb_msg_t rb;
    int ret = rb_event_peek(&rb);
    if (ret <= 0)
        return 0;

    copy_rb_msg_to_MSG(&rb, lpMsg);

    /* WM_QUIT returns 0 from PeekMessageA (matching Windows behavior) */
    if (lpMsg->message == WM_QUIT)
        return 0;

    /* Always return 1 when a message is available */
    return 1;
}

/* ── 3. TranslateMessage ──────────────────────────────────── */
/*
 * No-op. SDL-to-WM_* translation is already done inside rb_event_wait.
 * Returns 0 (Windows convention).
 */
KERNEL32_STUB
BOOL TranslateMessage(const MSG *lpMsg)
{
    (void)lpMsg;
    return 0;
}

/* ── 4. DispatchMessageA ──────────────────────────────────── */
/*
 * Looks up the wine_window_entry for the HWND.
 * If found and it has a wnd_proc, calls it as WNDPROC.
 * If no entry or no proc, falls through to DefWindowProcA.
 */
KERNEL32_STUB
LRESULT DispatchMessageA(const MSG *lpMsg)
{
    if (!lpMsg)
        return 0;

#if defined(__i386__)
    return DefWindowProcA(lpMsg->hwnd, lpMsg->message,
                          lpMsg->wParam, lpMsg->lParam);
#else
    if (lpMsg->message == WM_CLOSE) {
        return DefWindowProcA(lpMsg->hwnd, lpMsg->message,
                              lpMsg->wParam, lpMsg->lParam);
    }

    wine_window_entry *entry = get_window_entry(lpMsg->hwnd);
    if (entry && entry->wnd_proc) {
        WNDPROC proc = (WNDPROC)entry->wnd_proc;
        return proc(lpMsg->hwnd, lpMsg->message, lpMsg->wParam, lpMsg->lParam);
    }
    return DefWindowProcA(lpMsg->hwnd, lpMsg->message,
                          lpMsg->wParam, lpMsg->lParam);
#endif
}

/* ── 5. PostMessageA ──────────────────────────────────────── */
/*
 * Builds an rb_msg_t from the parameters and pushes it into the
 * event queue via rb_event_push.
 * Returns TRUE on success.
 */
KERNEL32_STUB
BOOL PostMessageA(HWND hWnd, UINT Msg, WPARAM wParam, LPARAM lParam)
{
    rb_msg_t rb;
    memset(&rb, 0, sizeof(rb));
    rb.hwnd    = (uintptr_t)hWnd;
    rb.message = Msg;
    rb.wParam  = wParam;
    rb.lParam  = lParam;

    rb_event_push(&rb);
    return TRUE;
}

/* ── 6. PostQuitMessage ───────────────────────────────────── */
/*
 * Pushes a WM_QUIT message into the event queue.
 */
KERNEL32_STUB
void PostQuitMessage(int nExitCode)
{
    g_quit_pending = 1;
    g_quit_exit_code = nExitCode;

    rb_msg_t rb;
    memset(&rb, 0, sizeof(rb));
    rb.hwnd    = 0;
    rb.message = WM_QUIT;
    rb.wParam  = (uint32_t)nExitCode;
    rb.lParam  = 0;

    rb_event_push(&rb);
}

/* ── 7. SendMessageA ──────────────────────────────────────── */
/*
 * Special-cases:
 *   WM_GETTEXT     — strncpy entry->title into buffer pointed by lParam,
 *                    limited by wParam (max chars). Returns actual length.
 *   WM_SETTEXT     — strncpy lpString (lParam) into entry->title,
 *                    call rb_window_set_title. Returns TRUE.
 *   WM_GETMINMAXINFO — fill MINMAXINFO struct pointed by lParam with
 *                       default values.
 * Otherwise: dispatch directly to the window procedure.
 */
KERNEL32_STUB
LRESULT SendMessageA(HWND hWnd, UINT Msg, WPARAM wParam, LPARAM lParam)
{
    wine_window_entry *entry = get_window_entry(hWnd);

    switch (Msg) {
    case WM_GETTEXT: {
        if (!entry)
            return 0;
        char *buf = (char *)(intptr_t)lParam;
        int max = (int)wParam;
        if (max <= 0)
            return 0;
        if (!buf)
            return 0;
        strncpy(buf, entry->title, max);
        buf[max - 1] = '\0';
        return (LRESULT)strlen(buf);
    }

    case WM_SETTEXT: {
        const char *str = (const char *)(intptr_t)lParam;
        if (!entry)
            return 0;
        strncpy(entry->title, str ? str : "", sizeof(entry->title) - 1);
        entry->title[sizeof(entry->title) - 1] = '\0';
        rb_window_set_title(entry->sdl_window, entry->title);
        return (LRESULT)TRUE;
    }

    case WM_GETMINMAXINFO: {
        MINMAXINFO *info = (MINMAXINFO *)(intptr_t)lParam;
        if (info) {
            memset(info, 0, sizeof(*info));
            info->ptReserved.x       = 0;
            info->ptReserved.y       = 0;
            info->ptMaxSize.x        = 800;
            info->ptMaxSize.y        = 600;
            info->ptMaxPosition.x    = 0;
            info->ptMaxPosition.y    = 0;
            info->ptMinTrackSize.x   = 100;
            info->ptMinTrackSize.y   = 100;
            info->ptMaxTrackSize.x   = 4096;
            info->ptMaxTrackSize.y   = 4096;
        }
        return 0;
    }

    default:
        if (entry && entry->wnd_proc) {
            WNDPROC proc = (WNDPROC)entry->wnd_proc;
            return proc(hWnd, Msg, wParam, lParam);
        }
        return DefWindowProcA(hWnd, Msg, wParam, lParam);
    }
}

/* ── 8. DefWindowProcA ────────────────────────────────────── */
/*
 * Default window procedure: returns 0 for all messages.
 */
KERNEL32_STUB
LRESULT DefWindowProcA(HWND hWnd, UINT Msg, WPARAM wParam, LPARAM lParam)
{
    (void)wParam;
    (void)lParam;
    if (Msg == WM_CLOSE) {
        DestroyWindow(hWnd);
    }
    return 0;
}

/* ── 9. CallWindowProcA ───────────────────────────────────── */
/*
 * Calls the given WNDPROC function pointer directly.
 */
KERNEL32_STUB
LRESULT CallWindowProcA(WNDPROC lpPrevWndFunc, HWND hWnd,
                        UINT Msg, WPARAM wParam, LPARAM lParam)
{
    if (!lpPrevWndFunc)
        return 0;
    return lpPrevWndFunc(hWnd, Msg, wParam, lParam);
}

/* ── 10. SetWindowsHookExA ────────────────────────────────── */
/*
 * Stub: returns NULL (no hook support).
 */
KERNEL32_STUB
HHOOK SetWindowsHookExA(int idHook, WNDPROC lpfn, HINSTANCE hMod, DWORD dwThreadId)
{
    (void)idHook;
    (void)lpfn;
    (void)hMod;
    (void)dwThreadId;
    return FORCE_HANDLE_RETURN(0, HHOOK);
}

/* ── 11. UnhookWindowsHookEx ──────────────────────────────── */
/*
 * Stub: always returns TRUE.
 */
KERNEL32_STUB
BOOL UnhookWindowsHookEx(HHOOK hhk)
{
    (void)hhk;
    return TRUE;
}

/* ── 12. CallNextHookEx ───────────────────────────────────── */
/*
 * Stub: always returns 0.
 */
KERNEL32_STUB
LRESULT CallNextHookEx(HHOOK hhk, int nCode, WPARAM wParam, LPARAM lParam)
{
    (void)hhk;
    (void)nCode;
    (void)wParam;
    (void)lParam;
    return 0;
}

/* ── 13. SystemParametersInfoA ────────────────────────────── */
/*
 * Stub: always returns TRUE.
 */
KERNEL32_STUB
BOOL SystemParametersInfoA(UINT uiAction, UINT uiParam, void *pvParam, UINT fWinIni)
{
    (void)uiAction;
    (void)uiParam;
    (void)pvParam;
    (void)fWinIni;
    return TRUE;
}
