/*
 * user32_message.c
 *
 * USER32 message-loop exports. Dispatch/send/default-proc responsibilities live
 * in user32_message_dispatch.c; queue/filter/quit/hook state lives in
 * user32_message_queue.c.
 *
 * All exported functions use KERNEL32_STUB (stdcall on i386, ms_abi on x86_64)
 * to match the calling convention of guest PE binaries.
 *
 * NOTE: This file is excluded from the default build. It depends on backend
 * symbols (rb_event_wait, rb_event_peek, rb_event_push, etc.) that are only
 * available when the SDL2 backend is linked.
 */

#include <stdint.h>

#include "user32_message_priv.h"
#include "include/debug.h"
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
        user32_copy_rb_msg_to_msg(&rb, lpMsg);
        return 0;
    }

    if (!user32_fetch_queued_message(1, 1, hWnd, wMsgFilterMin, wMsgFilterMax, &rb))
        return 0;

    user32_copy_rb_msg_to_msg(&rb, lpMsg);
    (void)user32_call_keyboard_hook(&rb);

    if (lpMsg->message == WM_QUIT)
        return 0;
    return 1;
}

KERNEL32_STUB
BOOL PeekMessageA(MSG *lpMsg, HWND hWnd, UINT wMsgFilterMin,
                  UINT wMsgFilterMax, UINT wRemoveMsg)
{
    static uint32_t idle_poll_count = 0;

    (void)hWnd;
    (void)wMsgFilterMin;
    (void)wMsgFilterMax;

    if (!lpMsg)
        return FALSE;

    rb_msg_t rb;
    if (user32_synthesize_quit_message((wRemoveMsg & 0x0001) != 0,
                                       hWnd, wMsgFilterMin, wMsgFilterMax, &rb)) {
        user32_copy_rb_msg_to_msg(&rb, lpMsg);
        return TRUE;
    }

    int remove = (wRemoveMsg & 0x0001) != 0;
    if (!user32_fetch_queued_message(0, remove, hWnd, wMsgFilterMin, wMsgFilterMax, &rb)) {
        uint32_t count = ++idle_poll_count;

        if (debug_level_at_least(1) &&
            ((count & (count - 1)) == 0 || (count % 100000u) == 0)) {
            DEBUG("user32: PeekMessageA idle count=%u remove=%d hwnd=0x%lx",
                  count, remove, (unsigned long)hWnd);
        }
        return 0;
    }
    idle_poll_count = 0;

    user32_copy_rb_msg_to_msg(&rb, lpMsg);
    (void)user32_call_keyboard_hook(&rb);

    /* Always return 1 when a message is available */
    return 1;
}

KERNEL32_STUB
BOOL TranslateMessage(const MSG *lpMsg)
{
    (void)lpMsg;
    return 0;
}
