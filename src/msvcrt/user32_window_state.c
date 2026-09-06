/*
 * user32_window_state.c
 *
 * Handle-backed window property access helpers split out from the USER32
 * lifecycle file.
 */

#include "user32_priv.h"

__attribute__((weak))
LONG_PTR user32_dialog_get_window_long_ptr(HWND hWnd, int nIndex)
{
    (void)hWnd;
    (void)nIndex;
    return 0;
}

static LONG_PTR user32_get_window_long_ptr(wine_window_entry *entry, int nIndex)
{
    if (!entry)
        return 0;

    switch (nIndex) {
    case GWL_WNDPROC:
        return (LONG_PTR)(intptr_t)entry->wnd_proc;
    case GWL_STYLE:
        return (LONG_PTR)entry->style;
    case GWL_EXSTYLE:
        return (LONG_PTR)entry->ex_style;
    case GWL_HINSTANCE:
        return (LONG_PTR)(uintptr_t)entry->hinstance;
    case GWL_HWNDPARENT:
        return (LONG_PTR)(uintptr_t)entry->parent;
    case GWL_USERDATA:
        return (LONG_PTR)entry->user_data;
    case GWL_ID:
        return (LONG_PTR)(uintptr_t)entry->menu;
    default:
        return 0;
    }
}

static LONG_PTR user32_set_window_long_ptr(wine_window_entry *entry,
                                           int nIndex,
                                           LONG_PTR dwNewLong)
{
    LONG_PTR old_value;

    if (!entry)
        return 0;

    old_value = user32_get_window_long_ptr(entry, nIndex);
    switch (nIndex) {
    case GWL_WNDPROC:
        entry->wnd_proc = (void *)(intptr_t)dwNewLong;
        return old_value;
    case GWL_STYLE:
        entry->style = (uint32_t)dwNewLong;
        return old_value;
    case GWL_EXSTYLE:
        entry->ex_style = (uint32_t)dwNewLong;
        return old_value;
    case GWL_HINSTANCE:
        entry->hinstance = (HINSTANCE)(uintptr_t)dwNewLong;
        return old_value;
    case GWL_HWNDPARENT:
        entry->parent = (HWND)(uintptr_t)dwNewLong;
        return old_value;
    case GWL_USERDATA:
        entry->user_data = (uintptr_t)dwNewLong;
        return old_value;
    case GWL_ID:
        entry->menu = (HMENU)(uintptr_t)dwNewLong;
        return old_value;
    default:
        return 0;
    }
}

KERNEL32_STUB
LONG GetWindowLongA(HWND hwnd, int nIndex)
{
    wine_window_entry *entry = get_window_entry(hwnd);
    LONG_PTR dialog_value;

    if (!entry) {
        dialog_value = user32_dialog_get_window_long_ptr(hwnd, nIndex);
        return (LONG)dialog_value;
    }

    return (LONG)user32_get_window_long_ptr(entry, nIndex);
}

KERNEL32_STUB
LONG SetWindowLongA(HWND hwnd, int nIndex, LONG dwNewLong)
{
    wine_window_entry *entry = get_window_entry(hwnd);
    if (!entry)
        return 0;

    return (LONG)user32_set_window_long_ptr(entry, nIndex, (LONG_PTR)dwNewLong);
}

KERNEL32_STUB
LONG_PTR GetWindowLongPtrA(HWND hwnd, int nIndex)
{
    wine_window_entry *entry = get_window_entry(hwnd);
    LONG_PTR dialog_value;

    if (!entry) {
        dialog_value = user32_dialog_get_window_long_ptr(hwnd, nIndex);
        return dialog_value;
    }

    return user32_get_window_long_ptr(entry, nIndex);
}

KERNEL32_STUB
LONG_PTR SetWindowLongPtrA(HWND hwnd, int nIndex, LONG_PTR dwNewLong)
{
    wine_window_entry *entry = get_window_entry(hwnd);
    if (!entry)
        return 0;

    return user32_set_window_long_ptr(entry, nIndex, dwNewLong);
}
