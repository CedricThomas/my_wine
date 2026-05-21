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
#include <stdint.h>

#include "user32_priv.h"
#include "include/debug.h"
#include "../include/render_backend.h"
#include <stdio.h>
#ifdef MY_WINE32
#include "include/kernel32.h"
#endif

extern void rb_event_set_active_window(uintptr_t hwnd);
extern int rb_window_attach_guest_hwnd(rb_window_t win, uintptr_t hwnd);
KERNEL32_STUB BOOL AdjustWindowRectEx(RECT *lpRect, DWORD dwStyle, BOOL bMenu, DWORD dwExStyle);

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

static char *user32_strdup(const char *src)
{
    size_t len;
    char *copy;

    if (!src)
        return NULL;

    len = user32_strlen(src) + 1;
    copy = user32_alloc(len);
    if (!copy)
        return NULL;

    user32_memcpy(copy, src, len);
    return copy;
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
#define USER32_DESKTOP_HWND  ((HWND)(uintptr_t)0x7fffff00u)

/* ── Class table (linear search by strcmp) ─────────────────── */

/*
 * class_table: stores registered WNDCLASSA entries.
 * Atom = index + 1 (0 means unregistered / invalid).
 *
 * NOTE: No lock — PE32 is single-threaded, and DOOM95 does not
 * use multithreading. Add a spinlock if multithreading is needed.
 */
int g_user32_live_windows = 0;
int g_user32_window_create_attempted = 0;
HWND g_user32_active_window = 0;
HWND g_user32_focus_window = 0;
typedef struct {
    WNDCLASSA wndclass;
} user32_class_entry;

static user32_class_entry *g_class_registry = NULL;
static int g_class_count = 0;
static int g_class_capacity = 0;

static user32_class_entry *get_class_entry(int idx)
{
    if (idx < 0 || idx >= g_class_count)
        return NULL;
    return &g_class_registry[idx];
}

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

static void user32_update_window_ownership_after_destroy(HWND destroyed_hwnd)
{
    HWND replacement = user32_find_replacement_window(destroyed_hwnd);

    if (g_user32_active_window == destroyed_hwnd)
        g_user32_active_window = replacement;
    if (g_user32_focus_window == destroyed_hwnd)
        g_user32_focus_window = replacement;
    rb_event_set_active_window((uintptr_t)user32_get_active_window());
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

static void user32_set_foreground_focus(HWND target, int send_messages)
{
    HWND previous_focus = user32_get_focus_window();
    HWND previous_active = user32_get_active_window();

    user32_set_focus_window(target);
    user32_set_active_window(target);
    rb_event_set_active_window((uintptr_t)target);

    if (send_messages) {
        HWND previous = previous_focus ? previous_focus : previous_active;
        user32_send_focus_transition(previous, target);
    }
}

static int ensure_class_capacity(int needed_count)
{
    user32_class_entry *new_registry;
    int new_capacity;

    if (needed_count <= g_class_capacity)
        return TRUE;

    new_capacity = g_class_capacity ? g_class_capacity * 2 : 16;
    while (new_capacity < needed_count)
        new_capacity *= 2;

    new_registry = user32_alloc((size_t)new_capacity * sizeof(*new_registry));
    if (!new_registry)
        return FALSE;

    if (g_class_registry && g_class_count > 0)
        user32_memcpy(new_registry, g_class_registry,
                      (size_t)g_class_count * sizeof(*new_registry));

    user32_free(g_class_registry);
    g_class_registry = new_registry;
    g_class_capacity = new_capacity;
    return TRUE;
}

/* Helper: find a registered class by name. Returns index >= 0 or -1. */
static int find_class(const char *name)
{
    int i;
    for (i = 0; i < g_class_count; i++) {
        const char *class_name = g_class_registry[i].wndclass.lpszClassName;

        if (class_name && user32_strcmp(class_name, name) == 0) {
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
    char *class_name_copy;
    user32_class_entry *entry;
    int idx;

    DEBUG_LEVEL(1, "user32: RegisterClassA class=%s wndproc=%p",
                (lpWndClass && lpWndClass->lpszClassName) ? lpWndClass->lpszClassName : "(null)",
                lpWndClass ? lpWndClass->lpfnWndProc : NULL);
    if (!lpWndClass || !lpWndClass->lpszClassName)
        return 0;

    /* Check for duplicate — return existing atom */
    idx = find_class(lpWndClass->lpszClassName);
    if (idx >= 0)
        return (ATOM)(idx + 1);

    if (!ensure_class_capacity(g_class_count + 1))
        return 0;

    class_name_copy = user32_strdup(lpWndClass->lpszClassName);
    if (!class_name_copy)
        return 0;

    entry = &g_class_registry[g_class_count];
    user32_memcpy(&entry->wndclass, lpWndClass, sizeof(entry->wndclass));
    entry->wndclass.lpszClassName = class_name_copy;

    g_class_count++;
    DEBUG_LEVEL(1, "user32: RegisterClassA success atom=%d", g_class_count);
    return (ATOM)g_class_count;
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

    int cidx = find_class(lpClassName);
    if (cidx < 0)
        return FORCE_HANDLE_RETURN(0, HWND);

    user32_class_entry *class_entry = get_class_entry(cidx);
    const WNDCLASSA *wc;

    if (!class_entry)
        return FORCE_HANDLE_RETURN(0, HWND);
    wc = &class_entry->wndclass;

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
    entry->class_atom = (ATOM)(cidx + 1);
    user32_strncpy(entry->title, lpWindowName ? lpWindowName : "", sizeof(entry->title) - 1);
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

    switch (nCmdShow) {
    case SW_HIDE:
        rb_window_show(entry->sdl_window, 0);
        return TRUE;
    case SW_SHOWNORMAL:
    case SW_SHOW:
    case SW_SHOWNA:
    case SW_SHOWDEFAULT:
        rb_window_show(entry->sdl_window, 1);
        user32_set_foreground_focus(hwnd, 1);
        return TRUE;
    case SW_RESTORE:
        rb_window_restore(entry->sdl_window);
        user32_set_foreground_focus(hwnd, 1);
        return TRUE;
    case SW_SHOWMINIMIZED:
    case SW_MINIMIZE:
        rb_window_minimize(entry->sdl_window);
        return TRUE;
    case SW_SHOWMAXIMIZED:
        rb_window_maximize(entry->sdl_window);
        user32_set_foreground_focus(hwnd, 1);
        return TRUE;
    default:
        rb_window_show(entry->sdl_window, 1);
        user32_set_foreground_focus(hwnd, 1);
        return TRUE;
    }
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
    if (uFlags & SWP_HIDEWINDOW)
        rb_window_show(entry->sdl_window, 0);
    if (uFlags & SWP_SHOWWINDOW)
        rb_window_show(entry->sdl_window, 1);
    if (!(uFlags & SWP_NOACTIVATE) && !(uFlags & SWP_HIDEWINDOW))
        user32_set_foreground_focus(hwnd, 1);
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

    user32_strncpy(entry->title, lpString ? lpString : "", sizeof(entry->title) - 1);
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
    return FORCE_HANDLE_RETURN(USER32_DESKTOP_HWND, HWND);
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
    HWND prev = user32_get_focus_window();
    HWND target = get_window_entry(hwnd) ? hwnd : 0;

    user32_set_foreground_focus(target, 1);
    return FORCE_HANDLE_RETURN(prev, HWND);
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
