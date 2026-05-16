/*
 * user32_window.c
 *
 * 27 window lifecycle stub functions for user32.dll.
 * Implements: RegisterClassA, CreateWindowExA, DestroyWindow, ShowWindow,
 * SetWindowPos, MoveWindow, SetWindowTextA, GetWindowRect, GetClientRect,
 * GetWindowLongA, SetWindowLongA, IsWindow, EnableWindow, GetDesktopWindow,
 * GetActiveWindow, GetFocus, SetFocus, UpdateWindow, InvalidateRect,
 * ValidateRect, BeginPaint, EndPaint, MapWindowPoints, GetSystemMetrics,
 * AdjustWindowRect, AdjustWindowRectEx, GetDC, ReleaseDC.
 *
 * All exported functions use KERNEL32_STUB (stdcall on i386, ms_abi on x86_64)
 * to match the calling convention of guest PE binaries.
 */

#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#include "user32_priv.h"
#include "../include/render_backend.h"
#include <stdio.h>

extern void rb_event_set_active_window(uintptr_t hwnd);

/* ── Additional user32 constants not in user32_types.h ──────── */

#define LPRECT       RECT*
#define LPCRECT      const RECT*
#define CW_USEDEFAULT        0x80000000u
#define HWP_USEDEFAULT       0xFFFFFFFF

/* Lazy backend init — rb_init called once on first user32 call */
static int g_user32_backend_inited = 0;
static int g_user32_backend_available = 0;
static int user32_ensure_backend(void)
{
    if (!g_user32_backend_inited) {
        g_user32_backend_inited = 1;
        if (rb_init() != 0) {
            fprintf(stderr, "WARNING: rb_init failed, window operations will fail\n");
            g_user32_backend_available = 0;
        } else {
            g_user32_backend_available = 1;
        }
    }
    return g_user32_backend_available;
}

/* SWP_ flags for SetWindowPos */
#define SWP_NOSIZE           0x0001
#define SWP_NOMOVE           0x0002
#define SWP_NOZORDER         0x0004
#define SWP_NOREDRAW         0x0008
#define SWP_NOACTIVATE       0x0010
#define SWP_FRAMECHANGED     0x0020
#define SWP_SHOWWINDOW       0x0040
#define SWP_HIDEWINDOW       0x0080
#define SWP_NOCOPYBITS       0x0100
#define SWP_NOOWNERZORDER    0x0200
#define SWP_NOSENDCHANGING   0x0400
#define SWP_DRAWFRAME        SWP_FRAMECHANGED

/* Special HWND values */
#define HWND_TOP             ((void *)0)
#define HWND_BOTTOM          ((void *)1)
#define HWND_TOPMOST         ((void *)-1)
#define HWND_NOTOPMOST       ((void *)-2)

/* ── Class table (linear search by strcmp) ─────────────────── */

/*
 * class_table: stores registered WNDCLASSA entries.
 * Atom = index + 1 (0 means unregistered / invalid).
 *
 * NOTE: No lock — PE32 is single-threaded, and DOOM95 does not
 * use multithreading. Add a spinlock if multithreading is needed.
 */
WNDCLASSA class_table[16];
int g_user32_live_windows = 0;
int g_user32_window_create_attempted = 0;
HWND g_user32_active_window = 0;
HWND g_user32_focus_window = 0;
static int class_count = 0;

/* Helper: find a registered class by name. Returns index >= 0 or -1. */
static int find_class(const char *name)
{
    int i;
    for (i = 0; i < class_count; i++) {
        if (class_table[i].lpszClassName &&
            strcmp(class_table[i].lpszClassName, name) == 0) {
            return i;
        }
    }
    return -1;
}

/* ═══════════════════════════════════════════════════════════
 * 28 exported window functions
 * ═══════════════════════════════════════════════════════════ */

/* ── 1. RegisterClassA ─────────────────────────────────────── */
/*
 * Linear search class_table[16] by strcmp on lpszClassName.
 * Copies the WNDCLASSA struct and returns an ATOM (index+1).
 * Returns 0 on failure (full table or no name).
 */
KERNEL32_STUB
ATOM RegisterClassA(const WNDCLASSA *lpWndClass)
{
    if (!lpWndClass || !lpWndClass->lpszClassName)
        return 0;

    if (class_count >= 16)
        return 0;

    /* Check for duplicate — return existing atom */
    int idx = find_class(lpWndClass->lpszClassName);
    if (idx >= 0)
        return (ATOM)(idx + 1);

    class_count++;
    memcpy(&class_table[class_count - 1], lpWndClass, sizeof(WNDCLASSA));
    return (ATOM)class_count;
}

/* ── 2. CreateWindowExA ────────────────────────────────────── */
/*
 * Looks up the registered class, mallocs a wine_window_entry,
 * calls rb_window_create, allocates a HANDLE_TYPE_HWIN handle,
 * returns the handle via FORCE_PTR_RETURN.
 */
KERNEL32_STUB
HWND CreateWindowExA(DWORD dwExStyle, const char *lpClassName,
                     const char *lpWindowName, DWORD dwStyle,
                     int x, int y, int nWidth, int nHeight,
                     HWND hWndParent, HMENU hMenu,
                     HINSTANCE hInstance, void *lpParam)
{
    (void)dwExStyle;
    (void)hWndParent;
    (void)hMenu;
    (void)hInstance;
    (void)lpParam;

    g_user32_window_create_attempted = 1;

    if (!user32_ensure_backend())
        return FORCE_HANDLE_RETURN(0, HWND);

    int cidx = find_class(lpClassName);
    if (cidx < 0)
        return FORCE_HANDLE_RETURN(0, HWND);

    const WNDCLASSA *wc = &class_table[cidx];

    /* Allocate our wrapper entry */
    wine_window_entry *entry = malloc(sizeof(*entry));
    if (!entry)
        return FORCE_HANDLE_RETURN(0, HWND);

    entry->wnd_proc = wc->lpfnWndProc;
    entry->style = dwStyle;
    strncpy(entry->title, lpWindowName ? lpWindowName : "", sizeof(entry->title) - 1);
    entry->title[sizeof(entry->title) - 1] = '\0';

    /* Resolve CW_USEDEFAULT → centered; -1 for rb to pick auto */
    int px = (x == (int)CW_USEDEFAULT) ? -1 : x;
    int py = (y == (int)CW_USEDEFAULT) ? -1 : y;
    int pw = (nWidth == (int)CW_USEDEFAULT) ? 640 : nWidth;
    int ph = (nHeight == (int)CW_USEDEFAULT) ? 480 : nHeight;

    /* Build SDL flags from window style */
    uint32_t rb_flags = 0;
    if (dwStyle & WS_VISIBLE)      rb_flags |= RB_WINDOW_SHOWN;
    if (dwStyle & WS_THICKFRAME)   rb_flags |= RB_WINDOW_RESIZABLE;

    rb_window_t rb_win = rb_window_create(entry->title, px, py, pw, ph, rb_flags);
    if (!rb_win) {
        free(entry);
        return FORCE_HANDLE_RETURN(0, HWND);
    }

    entry->sdl_window = rb_win;

    /* Allocate the entry in the handle manager as HANDLE_TYPE_HWIN */
    uint64_t handle = wine_handle_alloc(HANDLE_TYPE_HWIN, entry);
    if (!handle) {
        rb_window_destroy(rb_win);
        free(entry);
        return FORCE_HANDLE_RETURN(0, HWND);
    }
    g_user32_live_windows++;
    user32_set_active_window((HWND)handle);
    user32_set_focus_window((HWND)handle);
    rb_event_set_active_window(handle);
    return FORCE_HANDLE_RETURN(handle, HWND);
}

/* ── 3. DestroyWindow ──────────────────────────────────────── */
/*
 * Retrieves the entry via the handle, calls rb_window_destroy on the
 * underlying SDL window handle, frees the handle and the entry struct.
 * Returns TRUE on success.
 */
KERNEL32_STUB
BOOL DestroyWindow(HWND hwnd)
{
    wine_window_entry *entry = get_window_entry(hwnd);
    if (!entry)
        return FALSE;

#if !defined(__i386__)
    if (entry->wnd_proc) {
        WNDPROC proc = (WNDPROC)entry->wnd_proc;
        proc(hwnd, WM_DESTROY, 0, 0);
    }
#endif

    rb_window_destroy(entry->sdl_window);
    wine_handle_free((uint32_t)hwnd);
    free(entry);
    if (g_user32_active_window == hwnd) {
        user32_set_active_window(0);
        rb_event_set_active_window(0);
    }
    if (g_user32_focus_window == hwnd)
        user32_set_focus_window(0);
    if (g_user32_live_windows > 0)
        g_user32_live_windows--;
    return TRUE;
}

/* ── 4. ShowWindow ─────────────────────────────────────────── */
/*
 * Maps nCmdShow to rb_window_show.
 */
KERNEL32_STUB
BOOL ShowWindow(HWND hwnd, int nCmdShow)
{
    wine_window_entry *entry = get_window_entry(hwnd);
    if (!entry)
        return FALSE;

    int show = 0;
    switch (nCmdShow) {
    case SW_HIDE:          show = 0; break;
    case SW_SHOWNORMAL:
    case SW_SHOW:
    case SW_RESTORE:
    case SW_SHOWNA:
    case SW_SHOWDEFAULT:   show = 1; break;
    case SW_SHOWMINIMIZED:
    case SW_MINIMIZE:      show = 0; break;
    case SW_SHOWMAXIMIZED: show = 1; break;
    default:               show = 1; break;
    }

    rb_window_show(entry->sdl_window, show);
    return TRUE;
}

/* ── 5. SetWindowPos ───────────────────────────────────────── */
/*
 * Calls rb_window_set_position and/or rb_window_set_size based on
 * the SWP_ flags. hWndInsertAfter is ignored.
 */
KERNEL32_STUB
BOOL SetWindowPos(HWND hwnd, HWND hWndInsertAfter,
                  int x, int y, int cx, int cy, UINT uFlags)
{
    (void)hWndInsertAfter;
    wine_window_entry *entry = get_window_entry(hwnd);
    if (!entry)
        return FALSE;

    if (!(uFlags & SWP_NOMOVE))
        rb_window_set_position(entry->sdl_window, x, y);
    if (!(uFlags & SWP_NOSIZE))
        rb_window_set_size(entry->sdl_window, cx, cy);
    return TRUE;
}

/* ── 6. MoveWindow ─────────────────────────────────────────── */
/*
 * Calls rb_window_set_position + rb_window_set_size.
 * bRepaint is ignored (repaint handled by SDL).
 */
KERNEL32_STUB
BOOL MoveWindow(HWND hwnd, int x, int y, int nWidth, int nHeight, BOOL bRepaint)
{
    (void)bRepaint;
    wine_window_entry *entry = get_window_entry(hwnd);
    if (!entry)
        return FALSE;

    rb_window_set_position(entry->sdl_window, x, y);
    rb_window_set_size(entry->sdl_window, nWidth, nHeight);
    return TRUE;
}

/* ── 7. SetWindowTextA ─────────────────────────────────────── */
/*
 * Copies lpString into entry->title and calls rb_window_set_title.
 */
KERNEL32_STUB
BOOL SetWindowTextA(HWND hwnd, const char *lpString)
{
    wine_window_entry *entry = get_window_entry(hwnd);
    if (!entry)
        return FALSE;

    strncpy(entry->title, lpString ? lpString : "", sizeof(entry->title) - 1);
    entry->title[sizeof(entry->title) - 1] = '\0';
    rb_window_set_title(entry->sdl_window, entry->title);
    return TRUE;
}

/* ── 8. GetWindowRect ──────────────────────────────────────── */
/*
 * Calls rb_window_get_rect and writes into *lpRect.
 */
KERNEL32_STUB
BOOL GetWindowRect(HWND hwnd, RECT *lpRect)
{
    wine_window_entry *entry = get_window_entry(hwnd);
    if (!entry || !lpRect)
        return FALSE;

    rb_rect_t r;
    if (rb_window_get_rect(entry->sdl_window, &r) != RB_OK)
        return FALSE;

    lpRect->left   = r.x;
    lpRect->top    = r.y;
    lpRect->right  = r.x + r.w;
    lpRect->bottom = r.y + r.h;
    return TRUE;
}

/* ── 9. GetClientRect ──────────────────────────────────────── */
/*
 * Calls rb_window_get_client_rect (which sets x=0, y=0) and writes
 * into *lpRect. Client rect is always (0, 0, w, h).
 */
KERNEL32_STUB
BOOL GetClientRect(HWND hwnd, RECT *lpRect)
{
    wine_window_entry *entry = get_window_entry(hwnd);
    if (!entry || !lpRect)
        return FALSE;

    rb_rect_t r;
    if (rb_window_get_client_rect(entry->sdl_window, &r) != RB_OK)
        return FALSE;

    lpRect->left   = 0;
    lpRect->top    = 0;
    lpRect->right  = r.w;
    lpRect->bottom = r.h;
    return TRUE;
}

/* ── 10. GetWindowLongA ────────────────────────────────────── */
/*
 * Accesses entry fields. GWL_WNDPROC for subclassing, GWL_STYLE for
 * window style. Other indices return 0.
 */
KERNEL32_STUB
LONG GetWindowLongA(HWND hwnd, int nIndex)
{
    wine_window_entry *entry = get_window_entry(hwnd);
    if (!entry)
        return 0;

    switch (nIndex) {
    case GWL_WNDPROC:
        return (LONG)(uintptr_t)entry->wnd_proc;
    case GWL_STYLE:
        return (LONG)entry->style;
    default:
        return 0;
    }
}

/* ── 11. SetWindowLongA ────────────────────────────────────── */
/*
 * Modifies entry fields. GWL_WNDPROC for subclassing, GWL_STYLE for
 * window style. Other indices are no-ops returning 0.
 */
KERNEL32_STUB
LONG SetWindowLongA(HWND hwnd, int nIndex, LONG dwNewLong)
{
    wine_window_entry *entry = get_window_entry(hwnd);
    if (!entry)
        return 0;

    switch (nIndex) {
    case GWL_WNDPROC: {
        LONG old = (LONG)(uintptr_t)entry->wnd_proc;
        entry->wnd_proc = (void *)(intptr_t)dwNewLong;
        return old;
    }
    case GWL_STYLE: {
        LONG old = (LONG)entry->style;
        entry->style = (uint32_t)dwNewLong;
        return old;
    }
    default:
        return 0;
    }
}

/* ── 12. IsWindow ──────────────────────────────────────────── */
/*
 * Checks if the handle is of type HANDLE_TYPE_HWIN.
 */
KERNEL32_STUB
BOOL IsWindow(HWND hwnd)
{
    if (!hwnd)
        return FALSE;
    return wine_handle_get_type((uint32_t)hwnd) == HANDLE_TYPE_HWIN;
}

/* ── 13. EnableWindow ──────────────────────────────────────── */
/* Stub: always returns TRUE. */
KERNEL32_STUB
BOOL EnableWindow(HWND hwnd, BOOL bEnable)
{
    (void)hwnd;
    (void)bEnable;
    return TRUE;
}

/* ── 14. GetDesktopWindow ──────────────────────────────────── */
/* Stub: returns a sentinel non-zero value (not a real handle). */
KERNEL32_STUB
HWND GetDesktopWindow(void)
{
    return FORCE_HANDLE_RETURN(1, HWND);
}

/* ── 15. GetActiveWindow ───────────────────────────────────── */
/* Stub: returns a sentinel non-zero value. */
KERNEL32_STUB
HWND GetActiveWindow(void)
{
    return FORCE_HANDLE_RETURN(user32_get_active_window(), HWND);
}

/* ── 16. GetFocus ──────────────────────────────────────────── */
/* Stub: returns a sentinel non-zero value. */
KERNEL32_STUB
HWND GetFocus(void)
{
    return FORCE_HANDLE_RETURN(user32_get_focus_window(), HWND);
}

/* ── 17. SetFocus ──────────────────────────────────────────── */
/* Stub: returns a sentinel non-zero value. */
KERNEL32_STUB
HWND SetFocus(HWND hwnd)
{
    HWND target = get_window_entry(hwnd) ? hwnd : 0;
    user32_set_focus_window(target);
    if (target) {
        user32_set_active_window(target);
        rb_event_set_active_window((uintptr_t)target);
    }
    return FORCE_HANDLE_RETURN(target, HWND);
}

/* ── 18. UpdateWindow ──────────────────────────────────────── */
/* Stub: always returns TRUE. */
KERNEL32_STUB
BOOL UpdateWindow(HWND hwnd)
{
    (void)hwnd;
    return TRUE;
}

/* ── 19. InvalidateRect ────────────────────────────────────── */
/* Stub: always returns TRUE. */
KERNEL32_STUB
BOOL InvalidateRect(HWND hwnd, const RECT *lpRect, BOOL bErase)
{
    (void)hwnd;
    (void)lpRect;
    (void)bErase;
    return TRUE;
}

/* ── 20. ValidateRect ──────────────────────────────────────── */
/* Stub: always returns TRUE. */
KERNEL32_STUB
BOOL ValidateRect(HWND hwnd, const RECT *lpRect)
{
    (void)hwnd;
    (void)lpRect;
    return TRUE;
}

/* ── 21. BeginPaint ────────────────────────────────────────── */
/*
 * Mock HDC via GetDC pattern: call rb_window_get_dc, allocate a
 * HANDLE_TYPE_DC handle, fill the PAINTSTRUCT.
 */
KERNEL32_STUB
HDC BeginPaint(HWND hwnd, PAINTSTRUCT *lpPaint)
{
    wine_window_entry *entry = get_window_entry(hwnd);
    if (!entry || !lpPaint)
        return 0;

    lpPaint->fErase = TRUE;
    lpPaint->fRestore = FALSE;
    lpPaint->fPaintValidateRect = TRUE;

    rb_window_get_client_rect(entry->sdl_window, &entry->client_rect);
    lpPaint->rcPaint.left   = 0;
    lpPaint->rcPaint.top    = 0;
    lpPaint->rcPaint.right  = entry->client_rect.w;
    lpPaint->rcPaint.bottom = entry->client_rect.h;

    rb_dc_t dc = rb_window_get_dc(entry->sdl_window);
    uint64_t handle = wine_handle_alloc(HANDLE_TYPE_DC, (void *)(uintptr_t)dc);
    if (!handle) {
        rb_window_release_dc(entry->sdl_window, dc);
        return FORCE_HANDLE_RETURN(0, HDC);
    }
    return FORCE_HANDLE_RETURN(handle, HDC);
}

/* ── 22. EndPaint ──────────────────────────────────────────── */
/*
 * Releases the DC: calls rb_window_release_dc on the underlying DC,
 * frees the DC handle.
 */
KERNEL32_STUB
BOOL EndPaint(HWND hwnd, const PAINTSTRUCT *lpPaint)
{
    if (!lpPaint)
        return FALSE;

    HDC hdc = (HDC)(uintptr_t)lpPaint->hdc;
    if (hdc) {
        wine_window_entry *entry = get_window_entry(hwnd);
        rb_dc_t dc = (rb_dc_t)(uintptr_t)wine_handle_get((uint32_t)hdc);
        rb_window_release_dc(entry ? entry->sdl_window : 0, dc);
        wine_handle_free((uint32_t)hdc);
    }
    return TRUE;
}

/* ── 23. MapWindowPoints ───────────────────────────────────── */
/*
 * Identity mapping: input = output. Returns nCount.
 */
KERNEL32_STUB
int MapWindowPoints(HWND hWndFrom, HWND hWndTo, POINT *lpPoints, UINT cPoints)
{
    (void)hWndFrom;
    (void)hWndTo;
    (void)lpPoints;
    return (int)cPoints;
}

/* ── 24. GetSystemMetrics ──────────────────────────────────── */
/*
 * SM_CXSCREEN → display width, SM_CYSCREEN → display height,
 * others → 0.
 *
 * Uses the rb_display_get_size backend helper (in rb_init.c)
 * to avoid pulling SDL2 headers into the msvcrt build.
 */
KERNEL32_STUB
int GetSystemMetrics(int nIndex)
{
    switch (nIndex) {
    case SM_CXSCREEN: {
        int w = 0, h = 0;
        rb_display_get_size(&w, &h);
        return w;
    }
    case SM_CYSCREEN: {
        int w = 0, h = 0;
        rb_display_get_size(&w, &h);
        return h;
    }
    default:
        return 0;
    }
}

/* ── 25. AdjustWindowRect ──────────────────────────────────── */
/*
 * Stub: *lpRect unchanged, returns TRUE.
 */
KERNEL32_STUB
BOOL AdjustWindowRect(LPRECT lpRect, DWORD dwStyle, BOOL bMenu)
{
    (void)lpRect;
    (void)dwStyle;
    (void)bMenu;
    return TRUE;
}

/* ── 26. AdjustWindowRectEx ────────────────────────────────── */
/*
 * Stub: *lpRect unchanged, returns TRUE.
 */
KERNEL32_STUB
BOOL AdjustWindowRectEx(LPRECT lpRect, DWORD dwStyle, BOOL bMenu, DWORD dwExStyle)
{
    (void)lpRect;
    (void)dwStyle;
    (void)bMenu;
    (void)dwExStyle;
    return TRUE;
}

/* ── 27. GetDC ─────────────────────────────────────────────── */
/*
 * Calls rb_window_get_dc, allocates a HANDLE_TYPE_DC handle.
 * Returns the handle via FORCE_PTR_RETURN.
 */
KERNEL32_STUB
HDC GetDC(HWND hwnd)
{
    wine_window_entry *entry = get_window_entry(hwnd);
    if (!entry)
        return FORCE_HANDLE_RETURN(0, HDC);

    rb_dc_t dc = rb_window_get_dc(entry->sdl_window);
    uint64_t handle = wine_handle_alloc(HANDLE_TYPE_DC, (void *)(uintptr_t)dc);
    if (!handle) {
        rb_window_release_dc(entry->sdl_window, dc);
        return FORCE_HANDLE_RETURN(0, HDC);
    }
    return FORCE_HANDLE_RETURN(handle, HDC);
}

/* ── 28. ReleaseDC ─────────────────────────────────────────── */
/*
 * Releases the DC: calls rb_window_release_dc, frees the handle.
 * Returns 1 (success).
 */
KERNEL32_STUB
int ReleaseDC(HWND hwnd, HDC hdc)
{
    wine_window_entry *entry = get_window_entry(hwnd);
    if (!entry)
        return 0;

    rb_dc_t dc = (rb_dc_t)(uintptr_t)wine_handle_get((uint32_t)hdc);
    rb_window_release_dc(entry->sdl_window, dc);
    wine_handle_free((uint32_t)hdc);
    return 1;
}
