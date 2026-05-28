/*
 * rb_window_host.c
 *
 * SDL2 backend — host-stack SDL window operation helpers.
 */

#include "rb_sdl2_priv.h"

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
    int show;
} rb_sdl_window_show_args;

typedef struct {
    SDL_Window *window;
    int x;
    int y;
} rb_sdl_window_position_args;

typedef struct {
    SDL_Window *window;
    int width;
    int height;
} rb_sdl_window_size_args;

typedef struct {
    SDL_Window *window;
    const char *title;
} rb_sdl_window_title_args;

typedef struct {
    SDL_Window *window;
    rb_rect_t *rect;
} rb_sdl_window_rect_args;

typedef struct {
    SDL_Window *old_window;
    SDL_Window *new_window;
    int fullscreen;
    int width;
    int height;
} rb_sdl_window_fullscreen_args;

typedef struct {
    SDL_Window *window;
    int x;
    int y;
} rb_sdl_warp_mouse_args;

static uintptr_t rb_sdl_create_window_call(void *arg)
{
    rb_sdl_create_window_args *a = arg;
    return (uintptr_t)SDL_CreateWindow(a->title, a->x, a->y, a->w, a->h, a->flags);
}

static uintptr_t rb_sdl_destroy_window_call(void *arg)
{
    SDL_DestroyWindow((SDL_Window *)arg);
    return 0;
}

static uintptr_t rb_sdl_show_raise_pump_call(void *arg)
{
    SDL_Window *window = arg;
    SDL_Surface *surface;
    uint32_t color;

    SDL_ShowWindow(window);
    SDL_RaiseWindow(window);
    SDL_PumpEvents();

    surface = SDL_GetWindowSurface(window);
    if (!surface)
        return 0;

    color = SDL_MapRGB(surface->format, 0, 0, 0);
    SDL_FillRect(surface, NULL, color);
    SDL_UpdateWindowSurface(window);
    SDL_PumpEvents();
    return 0;
}

static uintptr_t rb_sdl_pump_events_call(void *arg)
{
    (void)arg;
    SDL_PumpEvents();
    return 0;
}

static uintptr_t rb_sdl_window_show_call(void *arg)
{
    rb_sdl_window_show_args *a = arg;
    if (a->show)
        SDL_ShowWindow(a->window);
    else
        SDL_HideWindow(a->window);
    return 0;
}

static uintptr_t rb_sdl_window_minimize_call(void *arg)
{
    SDL_MinimizeWindow(((rb_sdl_window_show_args *)arg)->window);
    return 0;
}

static uintptr_t rb_sdl_window_maximize_call(void *arg)
{
    SDL_MaximizeWindow(((rb_sdl_window_show_args *)arg)->window);
    return 0;
}

static uintptr_t rb_sdl_window_restore_call(void *arg)
{
    SDL_RestoreWindow(((rb_sdl_window_show_args *)arg)->window);
    return 0;
}

static uintptr_t rb_sdl_window_set_position_call(void *arg)
{
    rb_sdl_window_position_args *a = arg;
    SDL_SetWindowPosition(a->window, a->x, a->y);
    return 0;
}

static uintptr_t rb_sdl_window_set_size_call(void *arg)
{
    rb_sdl_window_size_args *a = arg;
    SDL_SetWindowSize(a->window, a->width, a->height);
    return 0;
}

static uintptr_t rb_sdl_window_set_title_call(void *arg)
{
    rb_sdl_window_title_args *a = arg;
    SDL_SetWindowTitle(a->window, a->title);
    return 0;
}

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

static uintptr_t rb_sdl_window_set_fullscreen_call(void *arg)
{
    rb_sdl_window_fullscreen_args *a = arg;
    const char *title = SDL_GetWindowTitle(a->old_window);
    uint32_t flags = SDL_WINDOW_SHOWN;

    (void)a->fullscreen;
    a->new_window = SDL_CreateWindow(title,
                                     SDL_WINDOWPOS_CENTERED,
                                     SDL_WINDOWPOS_CENTERED,
                                     a->width, a->height,
                                     flags);
    if (!a->new_window)
        return 0;

    SDL_DestroyWindow(a->old_window);
    SDL_ShowWindow(a->new_window);
    SDL_RaiseWindow(a->new_window);
    SDL_PumpEvents();
    return 1;
}

static uintptr_t rb_sdl_window_get_surface_call(void *arg)
{
    return (uintptr_t)SDL_GetWindowSurface((SDL_Window *)arg);
}

static uintptr_t rb_sdl_warp_mouse_call(void *arg)
{
    rb_sdl_warp_mouse_args *a = arg;
    SDL_WarpMouseInWindow(a->window, a->x, a->y);
    return 0;
}

SDL_Window *rb_window_host_create(const char *title, int x, int y, int w,
                                  int h, uint32_t flags)
{
    rb_sdl_create_window_args args = { title, x, y, w, h, flags };
    return (SDL_Window *)rb_call_on_host_stack(rb_sdl_create_window_call, &args);
}

void rb_window_host_destroy(SDL_Window *window)
{
    rb_call_on_host_stack(rb_sdl_destroy_window_call, window);
}

void rb_window_host_show_created(SDL_Window *window)
{
    rb_call_on_host_stack(rb_sdl_show_raise_pump_call, window);
}

void rb_window_host_pump_events(void)
{
    rb_call_on_host_stack(rb_sdl_pump_events_call, NULL);
}

void rb_window_host_show(SDL_Window *window, int show)
{
    rb_sdl_window_show_args args = { window, show };
    rb_call_on_host_stack(rb_sdl_window_show_call, &args);
}

void rb_window_host_minimize(SDL_Window *window)
{
    rb_sdl_window_show_args args = { window, 0 };
    rb_call_on_host_stack(rb_sdl_window_minimize_call, &args);
}

void rb_window_host_maximize(SDL_Window *window)
{
    rb_sdl_window_show_args args = { window, 1 };
    rb_call_on_host_stack(rb_sdl_window_maximize_call, &args);
}

void rb_window_host_restore(SDL_Window *window)
{
    rb_sdl_window_show_args args = { window, 1 };
    rb_call_on_host_stack(rb_sdl_window_restore_call, &args);
}

void rb_window_host_set_position(SDL_Window *window, int x, int y)
{
    rb_sdl_window_position_args args = { window, x, y };
    rb_call_on_host_stack(rb_sdl_window_set_position_call, &args);
}

void rb_window_host_set_size(SDL_Window *window, int width, int height)
{
    rb_sdl_window_size_args args = { window, width, height };
    rb_call_on_host_stack(rb_sdl_window_set_size_call, &args);
}

void rb_window_host_set_title(SDL_Window *window, const char *title)
{
    rb_sdl_window_title_args args = { window, title };
    rb_call_on_host_stack(rb_sdl_window_set_title_call, &args);
}

void rb_window_host_get_rect(SDL_Window *window, rb_rect_t *rect)
{
    rb_sdl_window_rect_args args = { window, rect };
    rb_call_on_host_stack(rb_sdl_window_get_rect_call, &args);
}

void rb_window_host_get_client_rect(SDL_Window *window, rb_rect_t *rect)
{
    rb_sdl_window_rect_args args = { window, rect };
    rb_call_on_host_stack(rb_sdl_window_get_client_rect_call, &args);
}

int rb_window_host_set_fullscreen(SDL_Window *old_window, SDL_Window **new_window,
                                  int fullscreen, int width, int height)
{
    rb_sdl_window_fullscreen_args args = {
        .old_window = old_window,
        .new_window = NULL,
        .fullscreen = fullscreen,
        .width = width,
        .height = height,
    };

    if (!rb_call_on_host_stack(rb_sdl_window_set_fullscreen_call, &args))
        return RB_FAIL;

    *new_window = args.new_window;
    return RB_OK;
}

SDL_Surface *rb_window_host_get_surface(SDL_Window *window)
{
    return (SDL_Surface *)rb_call_on_host_stack(rb_sdl_window_get_surface_call,
                                                window);
}

void rb_window_host_warp_mouse(SDL_Window *window, int x, int y)
{
    rb_sdl_warp_mouse_args args = { window, x, y };
    rb_call_on_host_stack(rb_sdl_warp_mouse_call, &args);
}
