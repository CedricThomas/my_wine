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

#endif /* MY_WINE_USER32_PRIV_H */
