/*
 * user32_window_lifecycle.c
 *
 * Backend/bootstrap and handle-backed create/destroy lifecycle helpers split
 * out from the USER32 export file.
 */

#include <stdio.h>

#include "user32_priv.h"
#include "include/debug.h"
#ifdef MY_WINE32
#include "include/kernel32.h"
#endif

extern int rb_window_attach_guest_hwnd(rb_window_t win, uintptr_t hwnd);

static int g_user32_backend_inited = 0;
static int g_user32_backend_available = 0;

void *user32_heap_alloc(size_t size)
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

void user32_heap_free(void *ptr)
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

BOOL user32_ensure_backend(void)
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

void user32_init_window_entry(wine_window_entry *entry, const WNDCLASSA *wc,
                              ATOM class_atom, DWORD dwExStyle, DWORD dwStyle,
                              HWND hWndParent, HMENU hMenu,
                              HINSTANCE hInstance, const char *lpWindowName)
{
    if (!entry || !wc)
        return;

    entry->wnd_proc = wc->lpfnWndProc;
    entry->class_name = wc->lpszClassName;
    entry->style = dwStyle;
    entry->ex_style = dwExStyle;
    entry->hinstance = hInstance;
    entry->parent = hWndParent;
    entry->menu = hMenu;
    entry->class_cursor = wc->hCursor;
    entry->class_atom = class_atom;
    user32_strncpy(entry->title, lpWindowName ? lpWindowName : "",
                   sizeof(entry->title) - 1);
    entry->title[sizeof(entry->title) - 1] = '\0';
}

rb_window_t user32_create_backend_window(const wine_window_entry *entry,
                                         DWORD dwStyle, int x, int y,
                                         int nWidth, int nHeight)
{
    uint32_t rb_flags = 0;

    if (!entry)
        return 0;

    if (dwStyle & WS_VISIBLE)
        rb_flags |= RB_WINDOW_SHOWN;
    if (dwStyle & WS_THICKFRAME)
        rb_flags |= RB_WINDOW_RESIZABLE;

    return rb_window_create(entry->title, x, y, nWidth, nHeight, rb_flags);
}

void user32_fill_create_struct(CREATESTRUCTA *create_struct, void *lpParam,
                               HINSTANCE hInstance, HMENU hMenu,
                               HWND hWndParent, int px, int py, int pw, int ph,
                               DWORD dwStyle, DWORD dwExStyle,
                               const char *lpWindowName,
                               const char *lpClassName)
{
    if (!create_struct)
        return;

    user32_memset(create_struct, 0, sizeof(*create_struct));
    create_struct->lpCreateParams = lpParam;
    create_struct->hInstance = hInstance;
    create_struct->hMenu = hMenu;
    create_struct->hwndParent = hWndParent;
    create_struct->cy = ph;
    create_struct->cx = pw;
    create_struct->y = py;
    create_struct->x = px;
    create_struct->style = (LONG)dwStyle;
    create_struct->lpszName = lpWindowName;
    create_struct->lpszClass = lpClassName;
    create_struct->dwExStyle = dwExStyle;
}

void user32_cleanup_failed_create(wine_window_entry *entry, rb_window_t rb_win,
                                  uint64_t handle, int decrement_live_windows)
{
    if (decrement_live_windows && g_user32_live_windows > 0)
        g_user32_live_windows--;
    if (rb_win)
        rb_window_destroy(rb_win);
    if (handle)
        wine_handle_free((uint32_t)handle);
    if (entry)
        user32_heap_free(entry);
}

BOOL user32_finish_window_create(wine_window_entry *entry,
                                 const CREATESTRUCTA *create_struct,
                                 DWORD dwStyle, int px, int py, int pw, int ph,
                                 uint64_t *handle_out)
{
    rb_window_t rb_win;
    uint64_t handle;
    LRESULT create_result;

    if (!entry || !create_struct || !handle_out)
        return FALSE;

    rb_win = user32_create_backend_window(entry, dwStyle, px, py, pw, ph);
    if (!rb_win) {
        DEBUG_LEVEL(1, "user32: CreateWindowExA rb_window_create failed");
        user32_cleanup_failed_create(entry, 0, 0, 0);
        return FALSE;
    }

    entry->sdl_window = rb_win;
    handle = wine_handle_alloc(HANDLE_TYPE_HWIN, entry);
    if (!handle) {
        DEBUG_LEVEL(1, "user32: CreateWindowExA handle alloc failed");
        user32_cleanup_failed_create(entry, rb_win, 0, 0);
        return FALSE;
    }

    g_user32_live_windows++;
    if (rb_window_attach_guest_hwnd(rb_win, handle) != RB_OK) {
        DEBUG_LEVEL(1, "user32: CreateWindowExA attach_guest_hwnd failed handle=0x%lx",
                    (unsigned long)handle);
        user32_cleanup_failed_create(entry, rb_win, handle, 1);
        return FALSE;
    }

    create_result = TRUE;
    if (entry->wnd_proc) {
        DEBUG_LEVEL(1, "user32: CreateWindowExA WM_NCCREATE hwnd=0x%lx wndproc=%p",
                    (unsigned long)handle, entry->wnd_proc);
        create_result = user32_call_wndproc((WNDPROC)entry->wnd_proc,
                                            (HWND)handle, WM_NCCREATE, 0,
                                            (LPARAM)(intptr_t)create_struct);
    }
    if (!create_result) {
        user32_cleanup_failed_create(entry, rb_win, handle, 1);
        return FALSE;
    }

    create_result = 0;
    if (entry->wnd_proc) {
        DEBUG_LEVEL(1, "user32: CreateWindowExA WM_CREATE hwnd=0x%lx",
                    (unsigned long)handle);
        create_result = user32_call_wndproc((WNDPROC)entry->wnd_proc,
                                            (HWND)handle, WM_CREATE, 0,
                                            (LPARAM)(intptr_t)create_struct);
    }
    if (create_result == (LRESULT)-1) {
        user32_cleanup_failed_create(entry, rb_win, handle, 1);
        return FALSE;
    }

    *handle_out = handle;
    return TRUE;
}

BOOL user32_finish_window_destroy(HWND hwnd, wine_window_entry *entry)
{
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
    user32_heap_free(entry);
    if (g_user32_live_windows > 0)
        g_user32_live_windows--;
    user32_update_window_ownership_after_destroy(hwnd);
    DEBUG_WRITE_ERR("user32: DestroyWindow end\n",
                    sizeof("user32: DestroyWindow end\n") - 1);
    return TRUE;
}
