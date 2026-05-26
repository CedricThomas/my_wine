/*
 * user32_focus.c
 *
 * Owns active/focus window policy and the focus-transition side effects shared
 * by window lifecycle code and backend event activation.
 */

#include "user32_priv.h"

extern void rb_event_set_active_window(uintptr_t hwnd) __attribute__((weak));

HWND g_user32_active_window = 0;
HWND g_user32_focus_window = 0;

static HWND user32_find_replacement_window(HWND exclude)
{
    uint32_t handle;

    for (handle = 1; handle <= HANDLE_TABLE_SIZE; handle++) {
        if ((HWND)(uintptr_t)handle == exclude)
            continue;
        if (wine_handle_get_type(handle) == HANDLE_TYPE_HWIN &&
            wine_handle_get(handle) != NULL)
            return (HWND)(uintptr_t)handle;
    }

    return 0;
}

static void user32_send_focus_transition(HWND previous, HWND target)
{
    wine_window_entry *prev_entry = get_window_entry(previous);
    wine_window_entry *target_entry = get_window_entry(target);

    if (previous == target)
        return;

    if (prev_entry && prev_entry->wnd_proc) {
        user32_call_wndproc((WNDPROC)prev_entry->wnd_proc, previous,
                            WM_ACTIVATE, WA_INACTIVE, (LPARAM)(uintptr_t)target);
        user32_call_wndproc((WNDPROC)prev_entry->wnd_proc, previous,
                            WM_KILLFOCUS, (WPARAM)(uintptr_t)target, 0);
    }

    if (target_entry && target_entry->wnd_proc) {
        user32_call_wndproc((WNDPROC)target_entry->wnd_proc, target,
                            WM_ACTIVATE, WA_ACTIVE, (LPARAM)(uintptr_t)previous);
        user32_call_wndproc((WNDPROC)target_entry->wnd_proc, target,
                            WM_SETFOCUS, (WPARAM)(uintptr_t)previous, 0);
    }
}

void user32_set_foreground_focus(HWND target, int send_messages)
{
    HWND previous_focus = user32_get_focus_window();
    HWND previous_active = user32_get_active_window();

    user32_set_focus_window(target);
    user32_set_active_window(target);
    if (rb_event_set_active_window)
        rb_event_set_active_window((uintptr_t)target);

    if (send_messages) {
        HWND previous = previous_focus ? previous_focus : previous_active;
        user32_send_focus_transition(previous, target);
    }
}

void user32_activate_window_direct(uintptr_t hwnd)
{
    HWND target = (HWND)(uintptr_t)hwnd;

    if (!get_window_entry(target))
        return;
    user32_set_foreground_focus(target, 1);
}

void user32_update_window_ownership_after_destroy(HWND destroyed_hwnd)
{
    HWND replacement = user32_find_replacement_window(destroyed_hwnd);

    if (g_user32_active_window == destroyed_hwnd)
        g_user32_active_window = replacement;
    if (g_user32_focus_window == destroyed_hwnd)
        g_user32_focus_window = replacement;
    if (rb_event_set_active_window)
        rb_event_set_active_window((uintptr_t)user32_get_active_window());
}

KERNEL32_STUB
HWND GetActiveWindow(void)
{
    return FORCE_HANDLE_RETURN(user32_get_active_window(), HWND);
}

KERNEL32_STUB
HWND GetFocus(void)
{
    return FORCE_HANDLE_RETURN(user32_get_focus_window(), HWND);
}

KERNEL32_STUB
HWND SetFocus(HWND hwnd)
{
    HWND prev = user32_get_focus_window();
    HWND target = get_window_entry(hwnd) ? hwnd : 0;

    user32_set_foreground_focus(target, 1);
    return FORCE_HANDLE_RETURN(prev, HWND);
}
