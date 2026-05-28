/*
 * rb_window_state.c
 *
 * SDL2 backend — backend-local window ownership and state helpers.
 */

#include "rb_sdl2_priv.h"

typedef struct {
    SDL_Window *window;
    uint32_t window_id;
    uintptr_t native_window_id;
} rb_sdl_window_ids_args;

typedef struct {
    SDL_Cursor *cursor;
} rb_sdl_cursor_args;

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

static uintptr_t rb_sdl_set_cursor_call(void *arg)
{
    rb_sdl_cursor_args *a = arg;
    SDL_SetCursor(a->cursor);
    return 0;
}

static uintptr_t rb_sdl_get_default_cursor_call(void *arg)
{
    (void)arg;
    return (uintptr_t)SDL_GetDefaultCursor();
}

int rb_window_refresh_ids(rb_window *wnd)
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

void rb_window_set_default_cursor(rb_window *wnd)
{
    SDL_Cursor *cursor;
    rb_sdl_cursor_args args;

    if (!wnd || !wnd->window)
        return;

    cursor = (SDL_Cursor *)rb_call_on_host_stack(rb_sdl_get_default_cursor_call, NULL);
    if (!cursor)
        return;

    args.cursor = cursor;
    rb_call_on_host_stack(rb_sdl_set_cursor_call, &args);
}

void rb_window_detach_surfaces(rb_window *wnd)
{
    if (!wnd)
        return;

    if (wnd->primary_surface) {
        rb_surface *primary = NULL;

        if (wine_handle_get_type((uint32_t)wnd->primary_surface) == HANDLE_TYPE_RB_SURFACE)
            primary = (rb_surface *)wine_handle_get((uint32_t)wnd->primary_surface);
        if (primary)
            primary->window = 0;
        wnd->primary_surface = 0;
    }

    if (wnd->backbuffer) {
        rb_surface *backbuffer = NULL;

        if (wine_handle_get_type((uint32_t)wnd->backbuffer) == HANDLE_TYPE_RB_SURFACE)
            backbuffer = (rb_surface *)wine_handle_get((uint32_t)wnd->backbuffer);
        if (backbuffer)
            backbuffer->window = 0;
        rb_surface_destroy(wnd->backbuffer);
        wnd->backbuffer = 0;
    }
}

void rb_window_rebind_guest(rb_window *wnd, rb_window_t win)
{
    extern void user32_activate_window_direct(uintptr_t hwnd)
        __attribute__((weak));

    if (!wnd || !wnd->guest_hwnd)
        return;

    rb_event_bind_window(wnd->guest_hwnd, win);
    rb_event_activate_window(wnd->guest_hwnd);
    if (user32_activate_window_direct)
        user32_activate_window_direct(wnd->guest_hwnd);
}

int rb_window_set_cursor_handle(rb_cursor_t cur)
{
    rb_cursor *cursor;
    rb_sdl_cursor_args args;

    if (wine_handle_get_type((uint32_t)cur) != HANDLE_TYPE_RB_CURSOR)
        return RB_FAIL;

    cursor = (rb_cursor *)wine_handle_get((uint32_t)cur);
    if (!cursor || !cursor->cursor)
        return RB_OK;

    args.cursor = cursor->cursor;
    rb_call_on_host_stack(rb_sdl_set_cursor_call, &args);
    return RB_OK;
}
