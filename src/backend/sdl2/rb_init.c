/*
 * rb_init.c
 *
 * SDL2 backend initialization and shutdown.
 */

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include "rb_sdl2_priv.h"
#include "include/common.h"
#include <stdio.h>
#include <stdlib.h>
#include <dlfcn.h>

static int (*g_prev_x_error_handler)(Display *, XErrorEvent *) = NULL;

#define RB_X11_BAD_WINDOW 3

rb_audio_state g_audio;

static int rb_x11_error_handler(Display *display, XErrorEvent *event)
{
    (void)display;
    if (event && event->error_code == RB_X11_BAD_WINDOW) {
        rb_runtime_note_bad_window((uintptr_t)event->resourceid);
        return 0;
    }

    if (g_prev_x_error_handler)
        return g_prev_x_error_handler(display, event);

    return 0;
}

static void rb_install_x11_error_handler(void)
{
    typedef int (*x_error_handler_fn)(Display *, XErrorEvent *);
    typedef x_error_handler_fn (*xset_error_handler_fn)(x_error_handler_fn);
    xset_error_handler_fn set_error_handler =
        (xset_error_handler_fn)dlsym(RTLD_DEFAULT, "XSetErrorHandler");

    if (set_error_handler)
        g_prev_x_error_handler = set_error_handler(rb_x11_error_handler);
}

static uintptr_t rb_sdl_quit_call(void *arg)
{
    (void)arg;
    SDL_Quit();
    return 0;
}

static uintptr_t rb_sdl_get_display_mode_call(void *arg)
{
    SDL_DisplayMode *mode = arg;
    return (uintptr_t)SDL_GetDisplayMode(0, 0, mode);
}

int rb_init(void)
{
    rb_backend_driver_info driver_info;

    if (rb_runtime_is_initialized()) {
        return 0;
    }

    memset(&driver_info, 0, sizeof(driver_info));
    rb_backend_capture_requested_drivers(&driver_info);

    rb_sdl_init_args args = {
        SDL_INIT_VIDEO | SDL_INIT_EVENTS | SDL_INIT_AUDIO,
        rb_install_x11_error_handler
    };
    if ((int)rb_call_on_host_stack(rb_backend_init_sdl_call, &args) < 0) {
        fprintf(stderr,
                "WARNING: SDL backends requested video=%s audio=%s active video=%s audio=%s\n",
                driver_info.requested_video_driver[0] ? driver_info.requested_video_driver : "auto",
                driver_info.requested_audio_driver[0] ? driver_info.requested_audio_driver : "auto",
                "unavailable", "unknown");
        fprintf(stderr, "WARNING: SDL_Init failed: %s\n", SDL_GetError());
        return RB_FAIL;
    }

    rb_call_on_host_stack(rb_backend_capture_active_drivers_call, &driver_info);
    DEBUG_LEVEL(1,
                "SDL backends requested video=%s audio=%s active video=%s audio=%s",
                driver_info.requested_video_driver[0] ? driver_info.requested_video_driver : "auto",
                driver_info.requested_audio_driver[0] ? driver_info.requested_audio_driver : "auto",
                driver_info.active_video_driver[0] ? driver_info.active_video_driver : "unknown",
                driver_info.active_audio_driver[0] ? driver_info.active_audio_driver : "unknown");

    g_audio.device_id = 0;
    g_audio.opened = 0;
    g_audio.sample_rate = 22050;
    g_audio.channels = 2;
    g_audio.bits_per_sample = 16;
    g_audio.buffer_size = 4096;
    g_audio.buffers = NULL;
    g_audio.buffer_count = 0;
    rb_runtime_consume_shutdown_request();
    rb_runtime_install_signal_handlers();
    rb_event_install_watch();

    rb_runtime_set_initialized(1);
    return RB_OK;
}

void rb_shutdown(void)
{
    if (!rb_runtime_is_initialized()) {
        return;
    }

    if (g_audio.opened) {
        rb_audio_close();
    }

    rb_call_on_host_stack(rb_sdl_quit_call, NULL);
    rb_runtime_restore_signal_handlers();
    rb_runtime_set_initialized(0);
}

void rb_display_get_size(int *out_w, int *out_h)
{
    if (out_w) *out_w = 0;
    if (out_h) *out_h = 0;
    if (!rb_runtime_is_initialized())
        return;
    SDL_DisplayMode mode;
    if ((int)rb_call_on_host_stack(rb_sdl_get_display_mode_call, &mode) == 0) {
        if (out_w) *out_w = mode.w;
        if (out_h) *out_h = mode.h;
    }
}
