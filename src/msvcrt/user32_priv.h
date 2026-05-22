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
    const char   *class_name;
    char          title[128];
    uint32_t      style;
    uint32_t      ex_style;
    uintptr_t     user_data;
    HINSTANCE     hinstance;
    HWND          parent;
    HMENU         menu;
    HCURSOR       class_cursor;
    ATOM          class_atom;
    rb_rect_t     client_rect;
    bool          destroy_in_progress;
} wine_window_entry;

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

static inline size_t user32_strlen(const char *s)
{
    size_t len = 0;

    if (!s)
        return 0;

    while (s[len] != '\0')
        len++;
    return len;
}

static inline int user32_strcmp(const char *a, const char *b)
{
    size_t i = 0;

    if (a == b)
        return 0;
    if (!a)
        return -1;
    if (!b)
        return 1;

    while (a[i] != '\0' && b[i] != '\0') {
        if ((unsigned char)a[i] != (unsigned char)b[i])
            return (unsigned char)a[i] - (unsigned char)b[i];
        i++;
    }

    return (unsigned char)a[i] - (unsigned char)b[i];
}

static inline void *user32_memcpy(void *dst, const void *src, size_t n)
{
    size_t i;
    unsigned char *d = dst;
    const unsigned char *s = src;

    if (!dst || !src)
        return dst;

    for (i = 0; i < n; i++)
        d[i] = s[i];
    return dst;
}

static inline void *user32_memset(void *dst, int value, size_t n)
{
    size_t i;
    unsigned char *d = dst;

    if (!dst)
        return dst;

    for (i = 0; i < n; i++)
        d[i] = (unsigned char)value;
    return dst;
}

static inline char *user32_strncpy(char *dst, const char *src, size_t n)
{
    size_t i = 0;

    if (!dst || n == 0)
        return dst;

    if (!src)
        src = "";

    while (i < n && src[i] != '\0') {
        dst[i] = src[i];
        i++;
    }
    while (i < n) {
        dst[i] = '\0';
        i++;
    }
    return dst;
}

int user32_dialog_run_modal(HWND hwnd, void *lpDialogFunc);
BOOL user32_dialog_end(HWND hDlg, intptr_t nResult);
LRESULT user32_dialog_send_control_message(HWND hWnd, UINT Msg, WPARAM wParam, LPARAM lParam);
LONG_PTR user32_dialog_get_window_long_ptr(HWND hWnd, int nIndex);

#endif /* MY_WINE_USER32_PRIV_H */
