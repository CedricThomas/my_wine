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
    if (wine_handle_get_type((uint32_t)win) != HANDLE_TYPE_RB_WINDOW)
        return NULL;
    return (rb_window *)wine_handle_get((uint32_t)win);
}

typedef struct {
    const char *title;
    int x;
    int y;
    int w;
    int h;
    uint32_t flags;
} rb_sdl_create_window_args;

typedef struct {
    SDL_Window *window;
    uint32_t window_id;
    uintptr_t native_window_id;
} rb_sdl_window_ids_args;

static uintptr_t rb_sdl_create_window_call(void *arg)
{
    rb_sdl_create_window_args *a = arg;
    return (uintptr_t)SDL_CreateWindow(a->title, a->x, a->y, a->w, a->h, a->flags);
}

static uintptr_t rb_sdl_window_get_ids_call(void *arg)
{
    rb_sdl_window_ids_args *a = arg;
    SDL_SysWMinfo info;

    a->window_id = 0;
    a->native_window_id = 0;
    if (!a->window)
        return 0;

    a->window_id = SDL_GetWindowID(a->window);
    SDL_VERSION(&info.version);
    if (SDL_GetWindowWMInfo(a->window, &info) &&
        info.subsystem == SDL_SYSWM_X11) {
        a->native_window_id = (uintptr_t)info.info.x11.window;
    }
    return 1;
}

static uintptr_t rb_sdl_destroy_window_call(void *arg)
{
    SDL_DestroyWindow((SDL_Window *)arg);
    return 0;
}

static uintptr_t rb_sdl_show_raise_pump_call(void *arg)
{
    SDL_Window *window = arg;
    SDL_ShowWindow(window);
    SDL_RaiseWindow(window);
    SDL_PumpEvents();

    SDL_Surface *surface = SDL_GetWindowSurface(window);
    if (surface) {
        uint32_t color = SDL_MapRGB(surface->format, 0, 0, 0);
        SDL_FillRect(surface, NULL, color);
        SDL_UpdateWindowSurface(window);
    }
    SDL_PumpEvents();
    return 0;
}

static uintptr_t rb_sdl_push_quit_if_autoquit_call(void *arg)
{
    (void)arg;
    if (rb_host_getenv("MY_WINE_SAMPLE_AUTOQUIT")) {
        SDL_Event ev;
        memset(&ev, 0, sizeof(ev));
        ev.type = SDL_QUIT;
        SDL_PushEvent(&ev);
    }
    return 0;
}

typedef struct {
    SDL_Window *window;
    int show;
} rb_sdl_window_show_args;

static uintptr_t rb_sdl_window_show_call(void *arg)
{
    rb_sdl_window_show_args *a = arg;
    if (a->show)
        SDL_ShowWindow(a->window);
    else
        SDL_HideWindow(a->window);
    return 0;
}

typedef struct {
    SDL_Window *window;
    int x;
    int y;
} rb_sdl_window_position_args;

static uintptr_t rb_sdl_window_set_position_call(void *arg)
{
    rb_sdl_window_position_args *a = arg;
    SDL_SetWindowPosition(a->window, a->x, a->y);
    return 0;
}

typedef struct {
    SDL_Window *window;
    int width;
    int height;
} rb_sdl_window_size_args;

static uintptr_t rb_sdl_window_set_size_call(void *arg)
{
    rb_sdl_window_size_args *a = arg;
    SDL_SetWindowSize(a->window, a->width, a->height);
    return 0;
}

typedef struct {
    SDL_Window *window;
    const char *title;
} rb_sdl_window_title_args;

static uintptr_t rb_sdl_window_set_title_call(void *arg)
{
    rb_sdl_window_title_args *a = arg;
    SDL_SetWindowTitle(a->window, a->title);
    return 0;
}

typedef struct {
    SDL_Window *window;
    rb_rect_t *rect;
} rb_sdl_window_rect_args;

static uintptr_t rb_sdl_window_get_rect_call(void *arg)
{
    rb_sdl_window_rect_args *a = arg;
    SDL_GetWindowPosition(a->window, &a->rect->x, &a->rect->y);
    SDL_GetWindowSize(a->window, &a->rect->w, &a->rect->h);
    return 0;
}

static uintptr_t rb_sdl_window_get_client_rect_call(void *arg)
{
    rb_sdl_window_rect_args *a = arg;
    SDL_GetWindowSize(a->window, &a->rect->w, &a->rect->h);
    return 0;
}

typedef struct {
    SDL_Window *old_window;
    SDL_Window *new_window;
    int fullscreen;
    int width;
    int height;
} rb_sdl_window_fullscreen_args;

static uintptr_t rb_sdl_window_set_fullscreen_call(void *arg)
{
    rb_sdl_window_fullscreen_args *a = arg;
    const char *title = SDL_GetWindowTitle(a->old_window);
    uint32_t flags = a->fullscreen ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0;

    a->new_window = SDL_CreateWindow(title,
                                     SDL_WINDOWPOS_CENTERED,
                                     SDL_WINDOWPOS_CENTERED,
                                     a->width, a->height,
                                     flags);
    if (!a->new_window)
        return 0;

    SDL_DestroyWindow(a->old_window);
    return 1;
}

static uintptr_t rb_sdl_window_get_surface_call(void *arg)
{
    return (uintptr_t)SDL_GetWindowSurface(((rb_window *)arg)->window);
}

typedef struct {
    SDL_Cursor *cursor;
} rb_sdl_cursor_args;

static uintptr_t rb_sdl_set_cursor_call(void *arg)
{
    rb_sdl_cursor_args *a = arg;
    SDL_SetCursor(a->cursor);
    return 0;
}

typedef struct {
    SDL_Window *window;
    int x;
    int y;
} rb_sdl_warp_mouse_args;

static uintptr_t rb_sdl_warp_mouse_call(void *arg)
{
    rb_sdl_warp_mouse_args *a = arg;
    SDL_WarpMouseInWindow(a->window, a->x, a->y);
    return 0;
}

static int rb_window_refresh_ids(rb_window *wnd)
{
    rb_sdl_window_ids_args args;

    if (!wnd || !wnd->window)
        return RB_FAIL;

    args.window = wnd->window;
    args.window_id = 0;
    args.native_window_id = 0;
    if (!rb_call_on_host_stack(rb_sdl_window_get_ids_call, &args))
        return RB_FAIL;

    wnd->sdl_window_id = args.window_id;
    wnd->native_window_id = args.native_window_id;
    return args.window_id ? RB_OK : RB_FAIL;
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

    rb_sdl_create_window_args args = { title, xpos, ypos, w, h, sdl_flags };
    SDL_Window *sdl_win = (SDL_Window *)rb_call_on_host_stack(rb_sdl_create_window_call, &args);
    if (!sdl_win)
        return 0;
    rb_call_on_host_stack(rb_sdl_show_raise_pump_call, sdl_win);
    rb_call_on_host_stack(rb_sdl_push_quit_if_autoquit_call, NULL);

    rb_window *win = rb_host_malloc(sizeof(*win));
    if (!win) {
        rb_call_on_host_stack(rb_sdl_destroy_window_call, sdl_win);
        return 0;
    }
    win->window = sdl_win;
    win->sdl_window_id = 0;
    win->native_window_id = 0;
    win->guest_hwnd = 0;
    win->primary_surface = 0;
    win->backbuffer = 0;
    if (rb_window_refresh_ids(win) != RB_OK) {
        rb_call_on_host_stack(rb_sdl_destroy_window_call, sdl_win);
        rb_host_free(win);
        return 0;
    }

    return (rb_window_t)wine_handle_alloc(HANDLE_TYPE_RB_WINDOW, win);
}

int rb_window_destroy(rb_window_t win)
{
    rb_window *w = get_window(win);
    if (!w)
        return RB_FAIL;

    /* Clean up flip-chain backbuffer owned by this window */
    if (w->backbuffer)
        rb_surface_destroy(w->backbuffer);

    if (w->guest_hwnd)
        rb_event_unbind_window(w->guest_hwnd);
    rb_call_on_host_stack(rb_sdl_destroy_window_call, w->window);
    rb_host_free(w);
    wine_handle_free((uint32_t)win);
    return RB_OK;
}

int rb_window_show(rb_window_t win, int show)
{
    rb_window *w = get_window(win);
    if (!w)
        return RB_FAIL;

    rb_sdl_window_show_args args = { w->window, show };
    rb_call_on_host_stack(rb_sdl_window_show_call, &args);
    return RB_OK;
}

int rb_window_set_position(rb_window_t win, int x, int y)
{
    rb_window *wnd = get_window(win);
    if (!wnd)
        return RB_FAIL;

    rb_sdl_window_position_args args = {
        wnd->window,
        x == RB_HINT_AUTO ? (int)SDL_WINDOWPOS_CENTERED : x,
        y == RB_HINT_AUTO ? (int)SDL_WINDOWPOS_CENTERED : y
    };
    rb_call_on_host_stack(rb_sdl_window_set_position_call, &args);
    return RB_OK;
}

int rb_window_set_size(rb_window_t win, int width, int height)
{
    rb_window *wnd = get_window(win);
    if (!wnd)
        return RB_FAIL;

    rb_sdl_window_size_args args = { wnd->window, width, height };
    rb_call_on_host_stack(rb_sdl_window_set_size_call, &args);
    return RB_OK;
}

int rb_window_set_title(rb_window_t win, const char *title)
{
    rb_window *w = get_window(win);
    if (!w)
        return RB_FAIL;

    rb_sdl_window_title_args args = { w->window, title };
    rb_call_on_host_stack(rb_sdl_window_set_title_call, &args);
    return RB_OK;
}

int rb_window_get_rect(rb_window_t win, rb_rect_t *rect)
{
    rb_window *w = get_window(win);
    if (!w)
        return RB_FAIL;

    rb_sdl_window_rect_args args = { w->window, rect };
    rb_call_on_host_stack(rb_sdl_window_get_rect_call, &args);
    return RB_OK;
}

int rb_window_get_client_rect(rb_window_t win, rb_rect_t *rect)
{
    rb_window *w = get_window(win);
    if (!w)
        return RB_FAIL;

    rect->x = 0;
    rect->y = 0;
    rb_sdl_window_rect_args args = { w->window, rect };
    rb_call_on_host_stack(rb_sdl_window_get_client_rect_call, &args);
    return RB_OK;
}

int rb_window_set_fullscreen(rb_window_t win, int fullscreen, int width, int height, int bpp)
{
    (void)bpp;
    rb_window *wnd = get_window(win);
    if (!wnd)
        return RB_FAIL;

    rb_sdl_window_fullscreen_args args = {
        .old_window = wnd->window,
        .new_window = NULL,
        .fullscreen = fullscreen,
        .width = width,
        .height = height,
    };
    if (!rb_call_on_host_stack(rb_sdl_window_set_fullscreen_call, &args))
        return RB_FAIL;

    wnd->window = args.new_window;
    if (rb_window_refresh_ids(wnd) != RB_OK)
        return RB_FAIL;
    if (wnd->guest_hwnd)
        rb_event_bind_window(wnd->guest_hwnd, win);
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

    SDL_Surface *surface = (SDL_Surface *)rb_call_on_host_stack(rb_sdl_window_get_surface_call, wnd);
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

    if (wine_handle_get_type((uint32_t)cur) != HANDLE_TYPE_RB_CURSOR)
        return RB_FAIL;

    rb_cursor *c = (rb_cursor *)wine_handle_get((uint32_t)cur);
    if (c && c->cursor) {
        rb_sdl_cursor_args args = { c->cursor };
        rb_call_on_host_stack(rb_sdl_set_cursor_call, &args);
    }
    return RB_OK;
}

int rb_window_warp_mouse(rb_window_t win, int x, int y)
{
    rb_window *wnd = get_window(win);
    if (!wnd)
        return RB_FAIL;

    rb_sdl_warp_mouse_args args = { wnd->window, x, y };
    rb_call_on_host_stack(rb_sdl_warp_mouse_call, &args);
    return RB_OK;
}
