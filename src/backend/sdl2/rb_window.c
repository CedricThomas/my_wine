/*
 * rb_window.c
 *
 * SDL2 backend — window lifecycle functions.
 * Implements all 13 window management functions declared in render_backend.h.
 */

#include "rb_sdl2_priv.h"
#include "include/debug.h"
#include <stdlib.h>
#include <string.h>

/* ---- helpers ---- */

static inline rb_window *get_window(rb_window_t win)
{
    if (wine_handle_get_type((uint32_t)win) != HANDLE_TYPE_RB_WINDOW)
        return NULL;
    return (rb_window *)wine_handle_get((uint32_t)win);
}

/* ---- 13 window lifecycle functions ---- */

rb_window_t rb_window_create(const char *title,
                             int x, int y, int w, int h,
                             uint32_t flags)
{
    uint32_t sdl_flags = 0;

    if (flags & RB_WINDOW_FULLSCREEN)
        sdl_flags |= SDL_WINDOW_FULLSCREEN_DESKTOP;
    if (flags & RB_WINDOW_RESIZABLE)
        sdl_flags |= SDL_WINDOW_RESIZABLE;
    if (flags & RB_WINDOW_SHOWN)
        sdl_flags |= SDL_WINDOW_SHOWN;
    else
        sdl_flags |= SDL_WINDOW_HIDDEN;

    int xpos = (x == RB_HINT_AUTO || x == -1) ? (int)SDL_WINDOWPOS_CENTERED : x;
    int ypos = (y == RB_HINT_AUTO || y == -1) ? (int)SDL_WINDOWPOS_CENTERED : y;

    SDL_Window *sdl_win = rb_window_host_create(title, xpos, ypos, w, h, sdl_flags);
    if (!sdl_win)
        return 0;
    if (flags & RB_WINDOW_SHOWN)
        rb_window_host_show_created(sdl_win);
    else
        rb_window_host_pump_events();

    rb_window *win = rb_host_malloc(sizeof(*win));
    if (!win) {
        rb_window_host_destroy(sdl_win);
        return 0;
    }
    win->window = sdl_win;
    win->sdl_window_id = 0;
    win->native_window_id = 0;
    win->guest_hwnd = 0;
    win->is_visible = (flags & RB_WINDOW_SHOWN) != 0;
    win->is_minimized = 0;
    win->is_maximized = 0;
    win->primary_surface = 0;
    win->backbuffer = 0;
    if (rb_window_refresh_ids(win) != RB_OK) {
        rb_window_host_destroy(sdl_win);
        rb_host_free(win);
        return 0;
    }
    rb_window_set_default_cursor(win);

    return (rb_window_t)wine_handle_alloc(HANDLE_TYPE_RB_WINDOW, win);
}

int rb_window_destroy(rb_window_t win)
{
    rb_window *w = get_window(win);
    if (!w)
        return RB_FAIL;

    DEBUG_WRITE_ERR("rb_window: destroy\n",
                    sizeof("rb_window: destroy\n") - 1);

    rb_window_detach_surfaces(w);

    if (w->guest_hwnd)
        rb_event_unbind_window(w->guest_hwnd);
    rb_window_host_destroy(w->window);
    rb_host_free(w);
    wine_handle_free((uint32_t)win);
    return RB_OK;
}

int rb_window_show(rb_window_t win, int show)
{
    rb_window *w = get_window(win);
    if (!w)
        return RB_FAIL;

    rb_window_host_show(w->window, show);
    w->is_visible = show != 0;
    if (!show) {
        w->is_minimized = 0;
        w->is_maximized = 0;
    }
    return RB_OK;
}

int rb_window_minimize(rb_window_t win)
{
    rb_window *w = get_window(win);
    if (!w)
        return RB_FAIL;

    rb_window_host_minimize(w->window);
    w->is_visible = 1;
    w->is_minimized = 1;
    w->is_maximized = 0;
    return RB_OK;
}

int rb_window_maximize(rb_window_t win)
{
    rb_window *w = get_window(win);
    if (!w)
        return RB_FAIL;

    rb_window_host_maximize(w->window);
    w->is_visible = 1;
    w->is_minimized = 0;
    w->is_maximized = 1;
    return RB_OK;
}

int rb_window_restore(rb_window_t win)
{
    rb_window *w = get_window(win);
    if (!w)
        return RB_FAIL;

    rb_window_host_restore(w->window);
    w->is_visible = 1;
    w->is_minimized = 0;
    w->is_maximized = 0;
    return RB_OK;
}

int rb_window_set_position(rb_window_t win, int x, int y)
{
    rb_window *wnd = get_window(win);
    if (!wnd)
        return RB_FAIL;

    rb_window_host_set_position(
        wnd->window,
        x == RB_HINT_AUTO ? (int)SDL_WINDOWPOS_CENTERED : x,
        y == RB_HINT_AUTO ? (int)SDL_WINDOWPOS_CENTERED : y);
    return RB_OK;
}

int rb_window_set_size(rb_window_t win, int width, int height)
{
    rb_window *wnd = get_window(win);
    if (!wnd)
        return RB_FAIL;

    rb_window_host_set_size(wnd->window, width, height);
    return RB_OK;
}

int rb_window_set_title(rb_window_t win, const char *title)
{
    rb_window *w = get_window(win);
    if (!w)
        return RB_FAIL;

    rb_window_host_set_title(w->window, title);
    return RB_OK;
}

int rb_window_get_rect(rb_window_t win, rb_rect_t *rect)
{
    rb_window *w = get_window(win);
    if (!w)
        return RB_FAIL;

    rb_window_host_get_rect(w->window, rect);
    return RB_OK;
}

int rb_window_get_client_rect(rb_window_t win, rb_rect_t *rect)
{
    rb_window *w = get_window(win);
    if (!w)
        return RB_FAIL;

    rect->x = 0;
    rect->y = 0;
    rb_window_host_get_client_rect(w->window, rect);
    return RB_OK;
}

int rb_window_set_fullscreen(rb_window_t win, int fullscreen, int width, int height, int bpp)
{
    (void)bpp;
    rb_window *wnd = get_window(win);
    if (!wnd)
        return RB_FAIL;

    SDL_Window *new_window = NULL;

    if (rb_window_host_set_fullscreen(wnd->window, &new_window, fullscreen,
                                      width, height) != RB_OK)
        return RB_FAIL;

    wnd->window = new_window;
    if (rb_window_refresh_ids(wnd) != RB_OK)
        return RB_FAIL;
    rb_window_set_default_cursor(wnd);
    rb_window_rebind_guest(wnd, win);
    return RB_OK;
}

int rb_window_attach_guest_hwnd(rb_window_t win, uintptr_t hwnd)
{
    rb_window *wnd = get_window(win);
    if (!wnd)
        return RB_FAIL;

    wnd->guest_hwnd = hwnd;
    return rb_event_bind_window(hwnd, win);
}

rb_dc_t rb_window_get_dc(rb_window_t win)
{
    rb_window *wnd = get_window(win);
    if (!wnd)
        return 0;

    SDL_Surface *surface = rb_window_host_get_surface(wnd->window);
    if (!surface)
        return 0;

    return (rb_dc_t)(uintptr_t)surface;
}

int rb_window_release_dc(rb_window_t win, rb_dc_t dc)
{
    /* No-op: SDL window surfaces are released via SDL_UpdateWindowSurface,
     * not an explicit unlock call. */
    (void)win;
    (void)dc;
    return RB_OK;
}

int rb_window_set_cursor(rb_window_t win, rb_cursor_t cur)
{
    rb_window *wnd = get_window(win);
    if (!wnd)
        return RB_FAIL;

    return rb_window_set_cursor_handle(cur);
}

int rb_window_warp_mouse(rb_window_t win, int x, int y)
{
    rb_window *wnd = get_window(win);
    if (!wnd)
        return RB_FAIL;

    rb_window_host_warp_mouse(wnd->window, x, y);
    return RB_OK;
}
