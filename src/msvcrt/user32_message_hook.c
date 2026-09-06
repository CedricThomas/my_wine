/*
 * user32_message_hook.c
 *
 * Keyboard-hook state and helpers shared by the USER32 message-loop exports.
 * Queue/filter/quit state stays in user32_message_queue.c.
 */

#include <stdint.h>

#include "user32_message_priv.h"
#include "include/debug.h"

typedef LRESULT (KERNEL32_ABI *HOOKPROC_WINE)(int nCode, WPARAM wParam, LPARAM lParam);

#define WH_KEYBOARD 2
#define HC_ACTION 0

static HOOKPROC_WINE g_keyboard_hook_proc = NULL;
static HHOOK g_keyboard_hook_handle = 0;

int user32_is_keyboard_message(UINT message)
{
    return message == WM_KEYDOWN || message == WM_KEYUP ||
           message == WM_SYSKEYDOWN || message == WM_SYSKEYUP;
}

HHOOK user32_set_keyboard_hook(void *proc)
{
    if (!proc)
        return FORCE_HANDLE_RETURN(0, HHOOK);

    g_keyboard_hook_proc = (HOOKPROC_WINE)proc;
    g_keyboard_hook_handle = (HHOOK)(uintptr_t)0x60000002u;
    return FORCE_HANDLE_RETURN(g_keyboard_hook_handle, HHOOK);
}

void user32_clear_keyboard_hook(HHOOK hook)
{
    if (hook != 0 && hook == g_keyboard_hook_handle) {
        g_keyboard_hook_proc = NULL;
        g_keyboard_hook_handle = 0;
    }
}

int user32_call_keyboard_hook(const rb_msg_t *msg)
{
    if (!msg || !g_keyboard_hook_proc || !user32_is_keyboard_message(msg->message))
        return 0;

    DEBUG_LEVEL(2, "user32: WH_KEYBOARD vk=0x%lx lp=0x%lx msg=0x%x",
                (unsigned long)msg->wParam, (unsigned long)msg->lParam,
                (unsigned)msg->message);
    return g_keyboard_hook_proc(HC_ACTION, msg->wParam, msg->lParam) != 0;
}

int user32_call_keyboard_hook_direct(uint32_t message, uint32_t wParam,
                                     intptr_t lParam)
{
    int result;

    if (!g_keyboard_hook_proc || !user32_is_keyboard_message(message))
        return 0;

    result = g_keyboard_hook_proc(HC_ACTION, (WPARAM)wParam,
                                  (LPARAM)lParam) != 0;
    DEBUG_LEVEL(2, "user32: WH_KEYBOARD direct vk=0x%x lp=0x%lx msg=0x%x result=%d",
                wParam, (unsigned long)(uintptr_t)lParam, message, result);
    return 1;
}

KERNEL32_STUB
HHOOK SetWindowsHookExA(int idHook, void *lpfn, HINSTANCE hMod, DWORD dwThreadId)
{
    (void)hMod;
    (void)dwThreadId;

    DEBUG_LEVEL(1, "user32: SetWindowsHookExA id=%d proc=%p", idHook, lpfn);
    if (idHook == WH_KEYBOARD)
        return user32_set_keyboard_hook(lpfn);

    return FORCE_HANDLE_RETURN(0, HHOOK);
}

KERNEL32_STUB
BOOL UnhookWindowsHookEx(HHOOK hhk)
{
    user32_clear_keyboard_hook(hhk);
    return TRUE;
}

KERNEL32_STUB
LRESULT CallNextHookEx(HHOOK hhk, int nCode, WPARAM wParam, LPARAM lParam)
{
    (void)hhk;
    (void)nCode;
    (void)wParam;
    (void)lParam;
    return 0;
}
