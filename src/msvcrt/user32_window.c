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

#include <stdint.h>

#include "user32_priv.h"
#include "include/debug.h"
#include "../include/render_backend.h"
#include <stdio.h>
#ifdef MY_WINE32
#include "include/kernel32.h"
#endif

extern int rb_window_attach_guest_hwnd(rb_window_t win, uintptr_t hwnd);
KERNEL32_STUB BOOL AdjustWindowRectEx(RECT *lpRect, DWORD dwStyle, BOOL bMenu, DWORD dwExStyle);
KERNEL32_STUB HCURSOR LoadCursorA(HINSTANCE hInstance, const char *lpCursorName);

__attribute__((weak))
LONG_PTR user32_dialog_get_window_long_ptr(HWND hWnd, int nIndex)
{
    (void)hWnd;
    (void)nIndex;
    return 0;
}

static void *user32_alloc(size_t size)
{
#ifdef MY_WINE32
    void *heap = GetProcessHeap();
    if (!heap)
        return NULL;
    return HeapAlloc(heap, 0, size);
#else
    return malloc(size);
#endif
}

static void user32_free(void *ptr)
{
#ifdef MY_WINE32
    void *heap = GetProcessHeap();
    if (!ptr)
        return;
    if (!heap)
        return;
    HeapFree(heap, 0, ptr);
#else
    free(ptr);
#endif
}

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

int g_user32_live_windows = 0;
int g_user32_window_create_attempted = 0;

/* ═══════════════════════════════════════════════════════════
 * 28 exported window functions
 * ═══════════════════════════════════════════════════════════ */

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
    CREATESTRUCTA create_struct;
    LRESULT create_result;

    (void)dwExStyle;

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

    /* Allocate our wrapper entry */
    wine_window_entry *entry = user32_alloc(sizeof(*entry));
    if (!entry)
        return FORCE_HANDLE_RETURN(0, HWND);
    user32_memset(entry, 0, sizeof(*entry));

    entry->wnd_proc = wc->lpfnWndProc;
    entry->class_name = wc->lpszClassName;
    entry->style = dwStyle;
    entry->ex_style = dwExStyle;
    entry->hinstance = hInstance;
    entry->parent = hWndParent;
    entry->menu = hMenu;
    entry->class_cursor = wc->hCursor;
    entry->class_atom = class_atom;
    user32_strncpy(entry->title, lpWindowName ? lpWindowName : "", sizeof(entry->title) - 1);
    entry->title[sizeof(entry->title) - 1] = '\0';
    if (entry->class_cursor == 0)
        entry->class_cursor = LoadCursorA(0, IDC_ARROW);

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
        DEBUG_LEVEL(1, "user32: CreateWindowExA rb_window_create failed");
        user32_free(entry);
        return FORCE_HANDLE_RETURN(0, HWND);
    }

    entry->sdl_window = rb_win;

    /* Allocate the entry in the handle manager as HANDLE_TYPE_HWIN */
    uint64_t handle = wine_handle_alloc(HANDLE_TYPE_HWIN, entry);
    if (!handle) {
        DEBUG_LEVEL(1, "user32: CreateWindowExA handle alloc failed");
        rb_window_destroy(rb_win);
        user32_free(entry);
        return FORCE_HANDLE_RETURN(0, HWND);
    }
    g_user32_live_windows++;
    if (rb_window_attach_guest_hwnd(rb_win, handle) != RB_OK) {
        DEBUG_LEVEL(1, "user32: CreateWindowExA attach_guest_hwnd failed handle=0x%lx",
                    (unsigned long)handle);
        g_user32_live_windows--;
        wine_handle_free((uint32_t)handle);
        rb_window_destroy(rb_win);
        user32_free(entry);
        return FORCE_HANDLE_RETURN(0, HWND);
    }

    user32_memset(&create_struct, 0, sizeof(create_struct));
    create_struct.lpCreateParams = lpParam;
    create_struct.hInstance = hInstance;
    create_struct.hMenu = hMenu;
    create_struct.hwndParent = hWndParent;
    create_struct.cy = ph;
    create_struct.cx = pw;
    create_struct.y = py;
    create_struct.x = px;
    create_struct.style = (LONG)dwStyle;
    create_struct.lpszName = lpWindowName;
    create_struct.lpszClass = wc->lpszClassName;
    create_struct.dwExStyle = dwExStyle;

    create_result = TRUE;
    if (entry->wnd_proc) {
        DEBUG_LEVEL(1, "user32: CreateWindowExA WM_NCCREATE hwnd=0x%lx wndproc=%p",
                    (unsigned long)handle, entry->wnd_proc);
        create_result = user32_call_wndproc((WNDPROC)entry->wnd_proc,
                                            (HWND)handle, WM_NCCREATE, 0,
                                            (LPARAM)(intptr_t)&create_struct);
    }
    if (!create_result) {
        g_user32_live_windows--;
        wine_handle_free((uint32_t)handle);
        rb_window_destroy(rb_win);
        user32_free(entry);
        return FORCE_HANDLE_RETURN(0, HWND);
    }

    create_result = 0;
    if (entry->wnd_proc) {
        DEBUG_LEVEL(1, "user32: CreateWindowExA WM_CREATE hwnd=0x%lx",
                    (unsigned long)handle);
        create_result = user32_call_wndproc((WNDPROC)entry->wnd_proc,
                                            (HWND)handle, WM_CREATE, 0,
                                            (LPARAM)(intptr_t)&create_struct);
    }
    if (create_result == (LRESULT)-1) {
        g_user32_live_windows--;
        wine_handle_free((uint32_t)handle);
        rb_window_destroy(rb_win);
        user32_free(entry);
        return FORCE_HANDLE_RETURN(0, HWND);
    }

    user32_set_foreground_focus((HWND)handle, 1);
    DEBUG_LEVEL(1, "user32: CreateWindowExA success hwnd=0x%lx",
                (unsigned long)handle);
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

    if (entry->destroy_in_progress)
        return TRUE;

    entry->destroy_in_progress = true;
    DEBUG_WRITE_ERR("user32: DestroyWindow begin\n",
                    sizeof("user32: DestroyWindow begin\n") - 1);
    if (entry->wnd_proc) {
        user32_call_wndproc((WNDPROC)entry->wnd_proc, hwnd, WM_DESTROY, 0, 0);
        user32_call_wndproc((WNDPROC)entry->wnd_proc, hwnd, WM_NCDESTROY, 0, 0);
    }

    rb_window_destroy(entry->sdl_window);
    wine_handle_free((uint32_t)hwnd);
    user32_free(entry);
    if (g_user32_live_windows > 0)
        g_user32_live_windows--;
    user32_update_window_ownership_after_destroy(hwnd);
    DEBUG_WRITE_ERR("user32: DestroyWindow end\n",
                    sizeof("user32: DestroyWindow end\n") - 1);
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
    LONG_PTR dialog_value;

    if (!entry) {
        dialog_value = user32_dialog_get_window_long_ptr(hwnd, nIndex);
        return (LONG)dialog_value;
    }

    return (LONG)user32_get_window_long_ptr(entry, nIndex);
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

/* ── 21. BeginPaint ────────────────────────────────────────── */
/*
 * Mock HDC via GetDC pattern: call rb_window_get_dc, allocate a
 * HANDLE_TYPE_DC handle, fill the PAINTSTRUCT.
 */
KERNEL32_STUB
HDC BeginPaint(HWND hwnd, PAINTSTRUCT *lpPaint)
{
    wine_window_entry *entry = get_window_entry(hwnd);
    rb_dc_t dc;
    uint64_t handle;

    if (!entry || !lpPaint)
        return 0;

    user32_memset(lpPaint, 0, sizeof(*lpPaint));
    lpPaint->fErase = TRUE;
    lpPaint->fRestore = FALSE;
    lpPaint->fPaintValidateRect = TRUE;

    rb_window_get_client_rect(entry->sdl_window, &entry->client_rect);
    lpPaint->rcPaint.left   = 0;
    lpPaint->rcPaint.top    = 0;
    lpPaint->rcPaint.right  = entry->client_rect.w;
    lpPaint->rcPaint.bottom = entry->client_rect.h;

    dc = rb_window_get_dc(entry->sdl_window);
    handle = wine_handle_alloc(HANDLE_TYPE_DC, (void *)(uintptr_t)dc);
    if (!handle) {
        rb_window_release_dc(entry->sdl_window, dc);
        return FORCE_HANDLE_RETURN(0, HDC);
    }
    lpPaint->hdc = (HDC)(uintptr_t)handle;
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
    wine_window_entry *entry;
    HDC hdc;
    rb_dc_t dc;

    if (!lpPaint)
        return FALSE;

    hdc = (HDC)(uintptr_t)lpPaint->hdc;
    if (!hdc)
        return TRUE;

    if (wine_handle_get_type((uint32_t)hdc) != HANDLE_TYPE_DC)
        return FALSE;

    entry = get_window_entry(hwnd);
    if (!entry)
        return FALSE;

    dc = (rb_dc_t)(uintptr_t)wine_handle_get((uint32_t)hdc);
    rb_window_release_dc(entry->sdl_window, dc);
    wine_handle_free((uint32_t)hdc);
    return TRUE;
}

/* ── 23. MapWindowPoints ───────────────────────────────────── */
/*
 * Identity mapping: input = output. Returns nCount.
 */
KERNEL32_STUB
int MapWindowPoints(HWND hWndFrom, HWND hWndTo, POINT *lpPoints, UINT cPoints)
{
    RECT from_rect;
    RECT to_rect;
    int dx = 0;
    int dy = 0;
    UINT i;

    if (!lpPoints && cPoints != 0)
        return 0;

    if (hWndFrom && GetWindowRect(hWndFrom, &from_rect)) {
        dx += from_rect.left;
        dy += from_rect.top;
    }
    if (hWndTo && GetWindowRect(hWndTo, &to_rect)) {
        dx -= to_rect.left;
        dy -= to_rect.top;
    }

    for (i = 0; i < cPoints; i++) {
        lpPoints[i].x += dx;
        lpPoints[i].y += dy;
    }
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
    int w = 0, h = 0;

    rb_display_get_size(&w, &h);
    switch (nIndex) {
    case SM_CXSCREEN:
    case SM_CXFULLSCREEN:
        return w;
    case SM_CYSCREEN:
    case SM_CYFULLSCREEN:
        return h;
    case SM_CXBORDER:
    case SM_CYBORDER:
        return 1;
    default:
        return 0;
    }
}

/* ── 25. AdjustWindowRect ──────────────────────────────────── */
/*
 * Stub: *lpRect unchanged, returns TRUE.
 */
KERNEL32_STUB
KERNEL32_STUB
BOOL AdjustWindowRect(LPRECT lpRect, DWORD dwStyle, BOOL bMenu)
{
    return AdjustWindowRectEx(lpRect, dwStyle, bMenu, 0);
}

static void user32_adjust_window_rect_impl(LPRECT lpRect, DWORD dwStyle,
                                           BOOL bMenu, DWORD dwExStyle)
{
    int left = 0;
    int right = 0;
    int top = 0;
    int bottom = 0;

    if (!lpRect)
        return;

    if (dwStyle & WS_CAPTION)
        top += 24;
    if (dwStyle & WS_THICKFRAME) {
        left += 4;
        right += 4;
        top += 4;
        bottom += 4;
    } else if (dwStyle & (WS_BORDER | WS_SYSMENU)) {
        left += 1;
        right += 1;
        top += 1;
        bottom += 1;
    }
    if (bMenu)
        top += 20;
    if (dwExStyle & WS_EX_WINDOWEDGE) {
        left += 2;
        right += 2;
        top += 2;
        bottom += 2;
    }

    lpRect->left -= left;
    lpRect->top -= top;
    lpRect->right += right;
    lpRect->bottom += bottom;
}

KERNEL32_STUB
BOOL AdjustWindowRectEx(LPRECT lpRect, DWORD dwStyle, BOOL bMenu, DWORD dwExStyle)
{
    user32_adjust_window_rect_impl(lpRect, dwStyle, bMenu, dwExStyle);
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

    if (wine_handle_get_type((uint32_t)hdc) != HANDLE_TYPE_DC)
        return 0;

    rb_dc_t dc = (rb_dc_t)(uintptr_t)wine_handle_get((uint32_t)hdc);
    rb_window_release_dc(entry->sdl_window, dc);
    wine_handle_free((uint32_t)hdc);
    return 1;
}
