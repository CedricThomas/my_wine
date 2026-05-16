#ifndef MY_WINE_USER32_PRIV_H
#define MY_WINE_USER32_PRIV_H

#include <stdint.h>
#include <string.h>
#include <stdbool.h>
#include "include/user32_types.h"
#include "include/wine_abi.h"
#include "include/handle_manager.h"
#include "include/render_backend.h"

/*
 * wine_window_entry — internal bookkeeping for each CreateWindowA call.
 * Indexed by the handle_manager slot assigned to the HWND.
 */
typedef struct {
    rb_window_t  sdl_window;
    void         *wnd_proc;
    char          title[128];
    uint32_t      style;
    rb_rect_t     client_rect;
    bool          destroy_in_progress;
} wine_window_entry;

/* Global WNDCLASSA table — linear search by strcmp on lpszClassName */
extern WNDCLASSA class_table[16];
extern int g_user32_live_windows;
extern int g_user32_window_create_attempted;
extern HWND g_user32_active_window;
extern HWND g_user32_focus_window;

/*
 * FORCE_HANDLE_RETURN(v, type) — like FORCE_PTR_RETURN but for integer
 * handle return types (HWND, HDC, HHOOK).  The macro returns void * but
 * the outer cast to the handle type suppresses the -Wint-conversion warning.
 */
#define FORCE_HANDLE_RETURN(v, type) ((type)(uintptr_t)FORCE_PTR_RETURN((void *)(uintptr_t)(v)))

/*
 * get_window_entry — look up the wine_window_entry for an HWND, or NULL.
 * Shared by user32_window.c and user32_message.c.
 */
static inline wine_window_entry *get_window_entry(HWND hwnd)
{
    void *obj = wine_handle_get((uint32_t)hwnd);
    if (!obj || wine_handle_get_type((uint32_t)hwnd) != HANDLE_TYPE_HWIN)
        return NULL;
    return (wine_window_entry *)obj;
}

static inline HWND user32_get_active_window(void)
{
    return get_window_entry(g_user32_active_window) ? g_user32_active_window : 0;
}

static inline void user32_set_active_window(HWND hwnd)
{
    g_user32_active_window = get_window_entry(hwnd) ? hwnd : 0;
}

static inline HWND user32_get_focus_window(void)
{
    return get_window_entry(g_user32_focus_window) ? g_user32_focus_window : 0;
}

static inline void user32_set_focus_window(HWND hwnd)
{
    g_user32_focus_window = get_window_entry(hwnd) ? hwnd : 0;
}

static inline LRESULT user32_call_wndproc(WNDPROC proc, HWND hwnd, UINT msg,
                                          WPARAM wParam, LPARAM lParam)
{
    if (!proc)
        return 0;
    return proc(hwnd, msg, wParam, lParam);
}

#endif /* MY_WINE_USER32_PRIV_H */
