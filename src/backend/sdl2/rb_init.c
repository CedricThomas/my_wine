/*
 * rb_init.c
 *
 * SDL2 backend initialization and shutdown.
 */

#include "rb_sdl2_priv.h"
#include <stdio.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <dlfcn.h>

static int g_initialized = 0;
static int (*g_prev_x_error_handler)(Display *, XErrorEvent *) = NULL;
static uintptr_t g_x11_bad_window_pending = 0;

#define RB_X11_BAD_WINDOW 3

rb_audio_state g_audio;
rb_audio_buf *g_audio_buffers[32];
int g_audio_buf_count = 0;

static int rb_x11_error_handler(Display *display, XErrorEvent *event)
{
    (void)display;
    if (event && event->error_code == RB_X11_BAD_WINDOW) {
        __atomic_store_n(&g_x11_bad_window_pending,
                         (uintptr_t)event->resourceid,
                         __ATOMIC_RELEASE);
        return 0;
    }

    if (g_prev_x_error_handler)
        return g_prev_x_error_handler(display, event);

    return 0;
}

uintptr_t rb_x11_consume_bad_window(void)
{
    return __atomic_exchange_n(&g_x11_bad_window_pending, 0, __ATOMIC_ACQ_REL);
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

typedef struct {
    uint32_t flags;
} rb_sdl_init_args;

static uintptr_t rb_sdl_init_call(void *arg)
{
    rb_sdl_init_args *a = arg;
    const char *requested_video_driver = getenv("SDL_VIDEODRIVER");
    int try_x11_fallback = 0;
    int try_dummy_fallback = 0;

    if (requested_video_driver == NULL || strcmp(requested_video_driver, "wayland") == 0)
        try_x11_fallback = 1;
    if (requested_video_driver == NULL || strcmp(requested_video_driver, "wayland") == 0)
        try_dummy_fallback = 1;

    SDL_SetHint(SDL_HINT_NO_SIGNAL_HANDLERS, "1");
    signal(SIGINT, SIG_DFL);
    signal(SIGTERM, SIG_DFL);
    rb_install_x11_error_handler();
    int ret = SDL_Init(a->flags);
    rb_install_x11_error_handler();
    if (ret < 0 && getenv("DISPLAY") && try_x11_fallback) {
        SDL_Quit();
        setenv("SDL_VIDEODRIVER", "x11", 1);
        ret = SDL_Init(a->flags);
        rb_install_x11_error_handler();
    }
    if (ret < 0 && try_dummy_fallback &&
        getenv("MY_WINE_SAMPLE_AUTOQUIT")) {
        SDL_Quit();
        setenv("SDL_VIDEODRIVER", "dummy", 1);
        ret = SDL_Init(a->flags);
        rb_install_x11_error_handler();
    }
    signal(SIGINT, SIG_DFL);
    signal(SIGTERM, SIG_DFL);
    return (uintptr_t)ret;
}

typedef struct {
    char active_video_driver[64];
    char active_audio_driver[64];
} rb_sdl_backend_info;

static uintptr_t rb_sdl_get_backend_info_call(void *arg)
{
    rb_sdl_backend_info *info = arg;
    const char *video = SDL_GetCurrentVideoDriver();
    const char *audio = SDL_GetCurrentAudioDriver();

    if (!info)
        return 0;

    if (video) {
        snprintf(info->active_video_driver, sizeof(info->active_video_driver), "%s", video);
    } else {
        info->active_video_driver[0] = '\0';
    }

    if (audio) {
        snprintf(info->active_audio_driver, sizeof(info->active_audio_driver), "%s", audio);
    } else {
        info->active_audio_driver[0] = '\0';
    }

    return 0;
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
    const char *requested_video_driver = getenv("SDL_VIDEODRIVER");
    const char *requested_audio_driver = getenv("SDL_AUDIODRIVER");

    if (g_initialized) {
        return 0;
    }

    rb_sdl_init_args args = { SDL_INIT_VIDEO | SDL_INIT_EVENTS };
    if ((int)rb_call_on_host_stack(rb_sdl_init_call, &args) < 0) {
        fprintf(stderr,
                "WARNING: SDL backends requested video=%s audio=%s active video=%s audio=%s\n",
                requested_video_driver ? requested_video_driver : "auto",
                requested_audio_driver ? requested_audio_driver : "auto",
                "unavailable", "unknown");
        fprintf(stderr, "WARNING: SDL_Init failed: %s\n", SDL_GetError());
        return RB_FAIL;
    }

    rb_sdl_backend_info backend_info;
    memset(&backend_info, 0, sizeof(backend_info));
    rb_call_on_host_stack(rb_sdl_get_backend_info_call, &backend_info);
    fprintf(stderr,
            "WARNING: SDL backends requested video=%s audio=%s active video=%s audio=%s\n",
            requested_video_driver ? requested_video_driver : "auto",
            requested_audio_driver ? requested_audio_driver : "auto",
            backend_info.active_video_driver[0] ? backend_info.active_video_driver : "unknown",
            backend_info.active_audio_driver[0] ? backend_info.active_audio_driver : "unknown");

    g_audio.device_id = 0;
    g_audio.opened = 0;
    g_audio.sample_rate = 22050;
    g_audio.channels = 2;
    g_audio.bits_per_sample = 16;
    g_audio.buffer_size = 4096;

    g_audio_buf_count = 0;
    memset(g_audio_buffers, 0, sizeof(g_audio_buffers));

    g_initialized = 1;
    return RB_OK;
}

void rb_shutdown(void)
{
    if (!g_initialized) {
        return;
    }

    if (g_audio.opened) {
        rb_audio_close();
    }

    rb_call_on_host_stack(rb_sdl_quit_call, NULL);
    g_initialized = 0;
}

void rb_display_get_size(int *out_w, int *out_h)
{
    if (out_w) *out_w = 0;
    if (out_h) *out_h = 0;
    if (!g_initialized)
        return;
    SDL_DisplayMode mode;
    if ((int)rb_call_on_host_stack(rb_sdl_get_display_mode_call, &mode) == 0) {
        if (out_w) *out_w = mode.w;
        if (out_h) *out_h = mode.h;
    }
}
