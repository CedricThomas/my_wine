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
} wine_window_entry;

/* Global WNDCLASSA table — linear search by strcmp on lpszClassName */
extern WNDCLASSA class_table[16];

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

#endif /* MY_WINE_USER32_PRIV_H */
