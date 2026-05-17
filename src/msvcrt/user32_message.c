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

#include <stdint.h>

#include "user32_priv.h"
#include "include/debug.h"
#include "../include/render_backend.h"

/* Forward declarations for cross-referenced stubs within this file */
KERNEL32_STUB LRESULT DefWindowProcA(HWND hWnd, UINT Msg, WPARAM wParam, LPARAM lParam);
KERNEL32_STUB BOOL DestroyWindow(HWND hwnd);

static int g_quit_pending = 0;
static int g_quit_exit_code = 0;
#define USER32_TRANSLATED_QUEUE_CAPACITY 64
#define USER32_POSTED_QUEUE_CAPACITY 64

static rb_msg_t g_translated_queue[USER32_TRANSLATED_QUEUE_CAPACITY];
static size_t g_translated_queue_head = 0;
static size_t g_translated_queue_count = 0;
static rb_msg_t g_posted_queue[USER32_POSTED_QUEUE_CAPACITY];
static size_t g_posted_queue_head = 0;
static size_t g_posted_queue_count = 0;

static int queue_push(rb_msg_t *queue, size_t capacity,
                      size_t *head, size_t *count,
                      const rb_msg_t *msg)
{
    size_t tail;

    if (!queue || !head || !count || !msg || *count >= capacity)
        return 0;

    tail = (*head + *count) % capacity;
    queue[tail] = *msg;
    (*count)++;
    return 1;
}

static int posted_queue_push(const rb_msg_t *msg)
{
    return queue_push(g_posted_queue, USER32_POSTED_QUEUE_CAPACITY,
                      &g_posted_queue_head, &g_posted_queue_count, msg);
}

static int translated_queue_push(const rb_msg_t *msg)
{
    return queue_push(g_translated_queue, USER32_TRANSLATED_QUEUE_CAPACITY,
                      &g_translated_queue_head, &g_translated_queue_count, msg);
}

static int message_target_alive(const rb_msg_t *msg)
{
    if (!msg)
        return 0;

    if (msg->message == WM_QUIT || msg->hwnd == 0)
        return 1;

    return get_window_entry((HWND)msg->hwnd) != NULL;
}

static int message_matches_filter(const rb_msg_t *msg, HWND hwnd_filter,
                                  UINT min_filter, UINT max_filter)
{
    UINT message;

    if (!msg)
        return 0;

    message = msg->message;
    if (message == WM_QUIT)
        return 1;

    if (hwnd_filter != 0 && msg->hwnd != (uintptr_t)hwnd_filter)
        return 0;

    if (min_filter == 0 && max_filter == 0)
        return 1;

    if (max_filter == 0)
        return message >= min_filter;

    return message >= min_filter && message <= max_filter;
}

static int user32_synthesize_quit_message(int remove, HWND hwnd_filter,
                                          UINT min_filter, UINT max_filter,
                                          rb_msg_t *msg)
{
    if (!msg)
        return 0;

    if (g_quit_pending) {
        user32_memset(msg, 0, sizeof(*msg));
        msg->message = WM_QUIT;
        msg->wParam = (WPARAM)g_quit_exit_code;
        if (!message_matches_filter(msg, hwnd_filter, min_filter, max_filter))
            return 0;
        if (remove)
            g_quit_pending = 0;
        return 1;
    }

    return 0;
}

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

static int queue_find_matching(const rb_msg_t *queue, size_t capacity,
                               size_t head, size_t count,
                               HWND hwnd_filter, UINT min_filter, UINT max_filter)
{
    size_t i;

    for (i = 0; i < count; i++) {
        size_t idx = (head + i) % capacity;
        if (message_matches_filter(&queue[idx], hwnd_filter, min_filter, max_filter))
            return (int)idx;
    }

    return -1;
}

static int queue_take_at(rb_msg_t *queue, size_t capacity,
                         size_t *head, size_t *count,
                         int match_idx, int remove, rb_msg_t *msg)
{
    size_t idx;
    size_t i;

    if (!queue || !head || !count || !msg || *count == 0 || match_idx < 0)
        return 0;

    idx = (size_t)match_idx;
    *msg = queue[idx];
    if (!remove)
        return 1;

    for (i = idx; i != ((*head + *count - 1) % capacity); i = (i + 1) % capacity) {
        size_t next = (i + 1) % capacity;
        queue[i] = queue[next];
    }
    (*count)--;
    return 1;
}

static void queue_discard_dead_targets(rb_msg_t *queue, size_t capacity,
                                       size_t *head, size_t *count)
{
    size_t old_count;
    size_t old_head;
    size_t write_count;
    size_t i;

    if (!queue || !head || !count || *count == 0)
        return;

    old_head = *head;
    old_count = *count;
    write_count = 0;

    for (i = 0; i < old_count; i++) {
        rb_msg_t msg = queue[(old_head + i) % capacity];

        if (!message_target_alive(&msg))
            continue;

        queue[(old_head + write_count) % capacity] = msg;
        write_count++;
    }

    *count = write_count;
}

static int posted_queue_take_matching(int remove, HWND hwnd_filter,
                                      UINT min_filter, UINT max_filter,
                                      rb_msg_t *msg)
{
    queue_discard_dead_targets(g_posted_queue, USER32_POSTED_QUEUE_CAPACITY,
                               &g_posted_queue_head, &g_posted_queue_count);

    int match_idx = queue_find_matching(g_posted_queue, USER32_POSTED_QUEUE_CAPACITY,
                                        g_posted_queue_head, g_posted_queue_count,
                                        hwnd_filter, min_filter, max_filter);
    return queue_take_at(g_posted_queue, USER32_POSTED_QUEUE_CAPACITY,
                         &g_posted_queue_head, &g_posted_queue_count,
                         match_idx, remove, msg);
}

static int translated_queue_take_matching(int remove, HWND hwnd_filter,
                                          UINT min_filter, UINT max_filter,
                                          rb_msg_t *msg)
{
    queue_discard_dead_targets(g_translated_queue, USER32_TRANSLATED_QUEUE_CAPACITY,
                               &g_translated_queue_head, &g_translated_queue_count);

    int match_idx = queue_find_matching(g_translated_queue,
                                        USER32_TRANSLATED_QUEUE_CAPACITY,
                                        g_translated_queue_head,
                                        g_translated_queue_count,
                                        hwnd_filter, min_filter, max_filter);
    return queue_take_at(g_translated_queue, USER32_TRANSLATED_QUEUE_CAPACITY,
                         &g_translated_queue_head, &g_translated_queue_count,
                         match_idx, remove, msg);
}

static int fetch_translated_message(int blocking, int remove,
                                    HWND hwnd_filter,
                                    UINT min_filter, UINT max_filter,
                                    rb_msg_t *msg)
{
    rb_msg_t incoming;
    int ret;

    if (!msg)
        return 0;

    if (posted_queue_take_matching(remove, hwnd_filter, min_filter, max_filter, msg))
        return 1;

    if (translated_queue_take_matching(remove, hwnd_filter, min_filter, max_filter, msg))
        return 1;

    for (;;) {
        user32_memset(&incoming, 0, sizeof(incoming));
        if (blocking) {
            ret = rb_event_wait(&incoming);
            if (ret < 0 || incoming.message == 0)
                return 0;
        } else {
            ret = rb_event_peek(&incoming);
            if (ret <= 0 || incoming.message == 0)
                return 0;
        }

        if (!message_target_alive(&incoming))
            continue;

        if (message_matches_filter(&incoming, hwnd_filter, min_filter, max_filter)) {
            if (!remove) {
                if (!translated_queue_push(&incoming))
                    return 0;
            }
            *msg = incoming;
            return 1;
        }

        if (!translated_queue_push(&incoming))
            return 0;
    }
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

    rb_msg_t rb;
    if (user32_synthesize_quit_message(1, hWnd, wMsgFilterMin, wMsgFilterMax, &rb)) {
        copy_rb_msg_to_MSG(&rb, lpMsg);
        return 0;
    }

    if (!fetch_translated_message(1, 1, hWnd, wMsgFilterMin, wMsgFilterMax, &rb))
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

    if (!lpMsg)
        return FALSE;

    rb_msg_t rb;
    if (user32_synthesize_quit_message((wRemoveMsg & 0x0001) != 0,
                                       hWnd, wMsgFilterMin, wMsgFilterMax, &rb)) {
        copy_rb_msg_to_MSG(&rb, lpMsg);
        return TRUE;
    }

    int remove = (wRemoveMsg & 0x0001) != 0;
    if (!fetch_translated_message(0, remove, hWnd, wMsgFilterMin, wMsgFilterMax, &rb))
        return 0;

    copy_rb_msg_to_MSG(&rb, lpMsg);

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

    if (lpMsg->message == WM_CLOSE || lpMsg->message == WM_DESTROY ||
        lpMsg->message == WM_QUIT || lpMsg->message == WM_SYSCOMMAND) {
        DEBUG_WRITE_ERR("user32: DispatchMessageA close-path\n",
                        sizeof("user32: DispatchMessageA close-path\n") - 1);
    }

    wine_window_entry *entry = get_window_entry(lpMsg->hwnd);
    if (entry && entry->wnd_proc) {
        return user32_call_wndproc((WNDPROC)entry->wnd_proc, lpMsg->hwnd,
                                   lpMsg->message, lpMsg->wParam, lpMsg->lParam);
    }
    return DefWindowProcA(lpMsg->hwnd, lpMsg->message,
                          lpMsg->wParam, lpMsg->lParam);
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

    if (hWnd != 0 && get_window_entry(hWnd) == NULL)
        return FALSE;

    user32_memset(&rb, 0, sizeof(rb));
    rb.hwnd    = (uintptr_t)hWnd;
    rb.message = Msg;
    rb.wParam  = wParam;
    rb.lParam  = lParam;

    return posted_queue_push(&rb) ? TRUE : FALSE;
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
    DEBUG_WRITE_ERR("user32: PostQuitMessage\n",
                    sizeof("user32: PostQuitMessage\n") - 1);
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

/* ── 8. DefWindowProcA ────────────────────────────────────── */
/*
 * Default window procedure:
 *   WM_NCCREATE  — returns TRUE (per Win32 spec, required for window creation).
 *   WM_CLOSE     — calls DestroyWindow, returns 0.
 *   WM_SYSCOMMAND/SC_CLOSE — calls DestroyWindow, returns 0.
 *   WM_SYSKEYDOWN/Alt+F4   — forwards as WM_SYSCOMMAND SC_CLOSE.
 *   All other messages — returns 0.
 */
KERNEL32_STUB
LRESULT DefWindowProcA(HWND hWnd, UINT Msg, WPARAM wParam, LPARAM lParam)
{
    const WPARAM sc_mask = 0xFFF0u;
    const WPARAM sc_close = 0xF060u;
    const LPARAM alt_context = (LPARAM)(1u << 29);

    if (Msg == WM_NCCREATE) {
        return TRUE;
    }
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

/* ── 9. CallWindowProcA ───────────────────────────────────── */
/*
 * Calls the given WNDPROC function pointer directly.
 */
KERNEL32_STUB
LRESULT CallWindowProcA(WNDPROC lpPrevWndFunc, HWND hWnd,
                        UINT Msg, WPARAM wParam, LPARAM lParam)
{
    return user32_call_wndproc(lpPrevWndFunc, hWnd, Msg, wParam, lParam);
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
