/*
 * user32_window.c
 *
 * Create/destroy window exports remain here as the public USER32 lifecycle
 * entry points, while backend/bootstrap helpers live in
 * user32_window_lifecycle.c and other window responsibilities are split into
 * dedicated translation units.
 */

#include "user32_priv.h"
#include "include/debug.h"

extern int rb_window_attach_guest_hwnd(rb_window_t win, uintptr_t hwnd);
KERNEL32_STUB HCURSOR LoadCursorA(HINSTANCE hInstance, const char *lpCursorName);

#define CW_USEDEFAULT 0x80000000u

KERNEL32_STUB
HWND CreateWindowExA(DWORD dwExStyle, const char *lpClassName,
                     const char *lpWindowName, DWORD dwStyle,
                     int x, int y, int nWidth, int nHeight,
                     HWND hWndParent, HMENU hMenu,
                     HINSTANCE hInstance, void *lpParam)
{
    CREATESTRUCTA create_struct;
    uint64_t handle = 0;

    g_user32_window_create_attempted = 1;
    DEBUG_LEVEL(1, "user32: CreateWindowExA class=%s title=%s style=0x%lx parent=0x%lx menu=0x%lx",
                lpClassName ? lpClassName : "(null)",
                lpWindowName ? lpWindowName : "(null)",
                (unsigned long)dwStyle,
                (unsigned long)(uintptr_t)hWndParent,
                (unsigned long)(uintptr_t)hMenu);

    if (!user32_ensure_backend())
        return FORCE_HANDLE_RETURN(0, HWND);

    ATOM class_atom = 0;
    const WNDCLASSA *wc = user32_find_registered_class(lpClassName, &class_atom);
    if (!wc)
        return FORCE_HANDLE_RETURN(0, HWND);

    wine_window_entry *entry = user32_heap_alloc(sizeof(*entry));
    if (!entry)
        return FORCE_HANDLE_RETURN(0, HWND);
    user32_memset(entry, 0, sizeof(*entry));
    user32_init_window_entry(entry, wc, class_atom, dwExStyle, dwStyle,
                             hWndParent, hMenu, hInstance, lpWindowName);
    if (entry->class_cursor == 0)
        entry->class_cursor = LoadCursorA(0, IDC_ARROW);

    {
        int px = (x == (int)CW_USEDEFAULT) ? -1 : x;
        int py = (y == (int)CW_USEDEFAULT) ? -1 : y;
        int pw = (nWidth == (int)CW_USEDEFAULT) ? 640 : nWidth;
        int ph = (nHeight == (int)CW_USEDEFAULT) ? 480 : nHeight;
        user32_fill_create_struct(&create_struct, lpParam, hInstance, hMenu,
                                  hWndParent, px, py, pw, ph, dwStyle,
                                  dwExStyle, lpWindowName, wc->lpszClassName);
        if (!user32_finish_window_create(entry, &create_struct, dwStyle,
                                         px, py, pw, ph, &handle)) {
            return FORCE_HANDLE_RETURN(0, HWND);
        }
    }

    user32_set_foreground_focus((HWND)handle, 1);
    DEBUG_LEVEL(1, "user32: CreateWindowExA success hwnd=0x%lx",
                (unsigned long)handle);
    return FORCE_HANDLE_RETURN(handle, HWND);
}

KERNEL32_STUB
BOOL DestroyWindow(HWND hwnd)
{
    wine_window_entry *entry = get_window_entry(hwnd);
    return user32_finish_window_destroy(hwnd, entry);
}
