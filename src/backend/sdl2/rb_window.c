/*
 * rb_window.c
 *
 * SDL2 backend — window lifecycle functions.
 * Implements all 13 window management functions declared in render_backend.h.
 */

#include "rb_sdl2_priv.h"
#include <stdlib.h>
#include <string.h>

/* ---- helpers ---- */

static inline rb_window *get_window(rb_window_t win)
{
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

    int xpos = (x == RB_HINT_AUTO || x == -1) ? (int)SDL_WINDOWPOS_CENTERED : x;
    int ypos = (y == RB_HINT_AUTO || y == -1) ? (int)SDL_WINDOWPOS_CENTERED : y;

    SDL_Window *sdl_win = SDL_CreateWindow(title, xpos, ypos, w, h, sdl_flags);
    if (!sdl_win)
        return 0;

    rb_window *win = malloc(sizeof(*win));
    if (!win) {
        SDL_DestroyWindow(sdl_win);
        return 0;
    }
    win->window = sdl_win;
    win->primary_surface = 0;
    win->backbuffer = 0;

    return (rb_window_t)wine_handle_alloc(HANDLE_TYPE_HWIN, win);
}

int rb_window_destroy(rb_window_t win)
{
    rb_window *w = get_window(win);
    if (!w)
        return RB_FAIL;

    /* Clean up flip-chain backbuffer owned by this window */
    if (w->backbuffer)
        rb_surface_destroy(w->backbuffer);

    SDL_DestroyWindow(w->window);
    free(w);
    wine_handle_free((uint32_t)win);
    return RB_OK;
}

int rb_window_show(rb_window_t win, int show)
{
    rb_window *w = get_window(win);
    if (!w)
        return RB_FAIL;

    if (show)
        SDL_ShowWindow(w->window);
    else
        SDL_HideWindow(w->window);
    return RB_OK;
}

int rb_window_set_position(rb_window_t win, int x, int y)
{
    rb_window *wnd = get_window(win);
    if (!wnd)
        return RB_FAIL;

    SDL_SetWindowPosition(wnd->window,
                          x == RB_HINT_AUTO ? (int)SDL_WINDOWPOS_CENTERED : x,
                          y == RB_HINT_AUTO ? (int)SDL_WINDOWPOS_CENTERED : y);
    return RB_OK;
}

int rb_window_set_size(rb_window_t win, int width, int height)
{
    rb_window *wnd = get_window(win);
    if (!wnd)
        return RB_FAIL;

    SDL_SetWindowSize(wnd->window, width, height);
    return RB_OK;
}

int rb_window_set_title(rb_window_t win, const char *title)
{
    rb_window *w = get_window(win);
    if (!w)
        return RB_FAIL;

    SDL_SetWindowTitle(w->window, title);
    return RB_OK;
}

int rb_window_get_rect(rb_window_t win, rb_rect_t *rect)
{
    rb_window *w = get_window(win);
    if (!w)
        return RB_FAIL;

    SDL_GetWindowPosition(w->window, &rect->x, &rect->y);
    SDL_GetWindowSize(w->window, &rect->w, &rect->h);
    return RB_OK;
}

int rb_window_get_client_rect(rb_window_t win, rb_rect_t *rect)
{
    rb_window *w = get_window(win);
    if (!w)
        return RB_FAIL;

    rect->x = 0;
    rect->y = 0;
    SDL_GetWindowSize(w->window, &rect->w, &rect->h);
    return RB_OK;
}

int rb_window_set_fullscreen(rb_window_t win, int fullscreen, int width, int height, int bpp)
{
    (void)bpp;
    rb_window *wnd = get_window(win);
    if (!wnd)
        return RB_FAIL;

    SDL_Window *old = wnd->window;
    const char *title = SDL_GetWindowTitle(old);

    uint32_t flags = fullscreen ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0;
    SDL_Window *new_win = SDL_CreateWindow(title,
                                            SDL_WINDOWPOS_CENTERED,
                                            SDL_WINDOWPOS_CENTERED,
                                            width, height,
                                            flags);
    if (!new_win)
        return RB_FAIL;

    wnd->window = new_win;
    SDL_DestroyWindow(old);
    return RB_OK;
}

rb_dc_t rb_window_get_dc(rb_window_t win)
{
    rb_window *wnd = get_window(win);
    if (!wnd)
        return 0;

    SDL_Surface *surface = SDL_GetWindowSurface(wnd->window);
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

    rb_cursor *c = (rb_cursor *)wine_handle_get((uint32_t)cur);
    if (c && c->cursor)
        SDL_SetCursor(c->cursor);
    return RB_OK;
}

int rb_window_warp_mouse(rb_window_t win, int x, int y)
{
    rb_window *wnd = get_window(win);
    if (!wnd)
        return RB_FAIL;

    SDL_WarpMouseInWindow(wnd->window, x, y);
    return RB_OK;
}
