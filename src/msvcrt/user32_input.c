/*
 * user32_input.c
 *
 * 8 input-related stub functions for user32.dll.
 * Implements: GetAsyncKeyState, LoadCursorA, SetCursor, SetCursorPos,
 *             ClipCursor, LoadIconA, wsprintfA, SetRect.
 *
 * All exported functions use KERNEL32_STUB (stdcall on i386, ms_abi on x86_64)
 * to match the calling convention of guest PE binaries.
 */

#include <stdio.h>
#include <stdarg.h>
#include <stdint.h>

#include "user32_priv.h"

/* ═══════════════════════════════════════════════════════════
 * 8 exported input functions
 * ═══════════════════════════════════════════════════════════ */

/* ── 1. GetAsyncKeyState ──────────────────────────────────── */
/*
 * Returns the state of the given virtual-key code.
 * Calls rb_keyboard_get_async_state(vKey) which returns int16_t:
 *   high bit set  → key is currently down
 *   low bit set   → key was pressed since last call
 */
KERNEL32_STUB
SHORT GetAsyncKeyState(int vKey)
{
    return rb_keyboard_get_async_state(vKey);
}

/* ── 2. LoadCursorA ───────────────────────────────────────── */
/*
 * Loads a system cursor by IDC_* constant (passed as pointer-cast int).
 * Recovers the numeric IDC via (int32_t)(uintptr_t)lpCursorName.
 * Calls rb_cursor_create(idc), allocates a HANDLE_TYPE_HCURSOR handle.
 * hInstance is ignored for system cursors (IDC_* values).
 */
KERNEL32_STUB
HCURSOR LoadCursorA(HINSTANCE hInstance, const char *lpCursorName)
{
    (void)hInstance;
    if (!lpCursorName)
        return FORCE_PTR_RETURN((void *)0);

    int idc = (int32_t)(uintptr_t)lpCursorName;
    rb_cursor_t cur = rb_cursor_create(idc);
    if (!cur)
        return FORCE_PTR_RETURN((void *)0);

    uint64_t handle = wine_handle_alloc(HANDLE_TYPE_HCURSOR, (void *)(uintptr_t)cur);
    if (!handle) {
        rb_cursor_destroy(cur);
        return FORCE_PTR_RETURN((void *)0);
    }
    return FORCE_PTR_RETURN((void *)handle);
}

/* ── 3. SetCursor ─────────────────────────────────────────── */
/*
 * Sets the current cursor. Retrieves the cursor from the handle manager,
 * finds the active window, and applies the cursor via rb_window_set_cursor.
 * Returns the previous cursor handle (or NULL on first call).
 */
KERNEL32_STUB
HCURSOR SetCursor(HCURSOR hCursor)
{
    rb_cursor_t cur = 0;
    if (hCursor) {
        cur = (rb_cursor_t)(uintptr_t)wine_handle_get((uint32_t)hCursor);
    }

    /* Find the first active window to set the cursor on.
     * In practice there is typically only one game window. */
    wine_handle_t *table = wine_handle_get((uint32_t)0);
    (void)table; /* suppress unused warning; we iterate differently */

    /* Iterate the handle table to find the first HWIN entry. */
    for (uint32_t i = 1; i <= HANDLE_TABLE_SIZE; i++) {
        if (wine_handle_get_type(i) == HANDLE_TYPE_HWIN) {
            wine_window_entry *entry = (wine_window_entry *)wine_handle_get(i);
            if (entry && entry->sdl_window) {
                if (cur) {
                    rb_window_set_cursor(entry->sdl_window, cur);
                }
                return FORCE_PTR_RETURN((void *)0); /* no previous cursor tracked */
            }
        }
    }

    return FORCE_PTR_RETURN((void *)0);
}

/* ── 4. SetCursorPos ──────────────────────────────────────── */
/*
 * Warps the mouse to the given screen coordinates.
 * x is in *Xparam (first arg), y is in *Yparam (second arg).
 * Finds the active window and calls rb_window_warp_mouse.
 */
KERNEL32_STUB
BOOL SetCursorPos(int Xparam, int Yparam)
{
    /* Find the first active window and warp mouse there. */
    for (uint32_t i = 1; i <= HANDLE_TABLE_SIZE; i++) {
        if (wine_handle_get_type(i) == HANDLE_TYPE_HWIN) {
            wine_window_entry *entry = (wine_window_entry *)wine_handle_get(i);
            if (entry && entry->sdl_window) {
                rb_window_warp_mouse(entry->sdl_window, Xparam, Yparam);
                return TRUE;
            }
        }
    }
    return FALSE;
}

/* ── 5. ClipCursor ────────────────────────────────────────── */
/*
 * Stub: always returns TRUE. Cursor clipping not implemented.
 */
KERNEL32_STUB
BOOL ClipCursor(const RECT *lpRect)
{
    (void)lpRect;
    return TRUE;
}

/* ── 6. LoadIconA ─────────────────────────────────────────── */
/*
 * Stub: returns a sentinel HICON value (non-zero).
 * Icon loading is not implemented; the sentinel satisfies callers
 * that check for a non-NULL icon handle.
 */
KERNEL32_STUB
HICON LoadIconA(HINSTANCE hInstance, const char *lpIconName)
{
    (void)hInstance;
    (void)lpIconName;
    return FORCE_PTR_RETURN((void *)0x12340001);
}

/* ── 7. wsprintfA ─────────────────────────────────────────── */
/*
 * Windows-style formatted string output. Wraps vsprintf.
 * Windows format strings use the same % syntax as printf, so no
 * translation is needed beyond the varargs forwarding.
 * Returns the number of characters written (excluding null terminator).
 */
KERNEL32_STUB
int wsprintfA(char *lpOut, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    int ret = vsprintf(lpOut, fmt, ap);
    va_end(ap);
    return ret;
}

/* ── 8. SetRect ───────────────────────────────────────────── */
/*
 * Initializes a RECT structure with the given coordinates.
 * Pure struct assignment, no backend call needed.
 */
KERNEL32_STUB
void SetRect(RECT *r, int x, int y, int x2, int y2)
{
    if (r) {
        r->left   = x;
        r->top    = y;
        r->right  = x2;
        r->bottom = y2;
    }
}
