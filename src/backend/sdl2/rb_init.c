/*
 * rb_init.c
 *
 * SDL2 backend initialization and shutdown.
 */

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include "rb_sdl2_priv.h"
#include <stdio.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <dlfcn.h>
#include <pthread.h>

static int g_initialized = 0;
static int (*g_prev_x_error_handler)(Display *, XErrorEvent *) = NULL;
static uintptr_t g_x11_bad_window_pending = 0;
static volatile sig_atomic_t g_shutdown_requested = 0;
static struct sigaction g_prev_sigint_action;
static struct sigaction g_prev_sigterm_action;
static int g_signal_handlers_installed = 0;
static pthread_t g_event_pump_thread;
static volatile int g_event_pump_thread_running = 0;

#define RB_X11_BAD_WINDOW 3

rb_audio_state g_audio;

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

static void rb_shutdown_signal_handler(int signum)
{
    (void)signum;
    g_shutdown_requested = 1;
}

static void rb_install_signal_handlers(void)
{
    struct sigaction sa;

    if (g_signal_handlers_installed)
        return;

    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = rb_shutdown_signal_handler;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGINT, &sa, &g_prev_sigint_action);
    sigaction(SIGTERM, &sa, &g_prev_sigterm_action);
    g_signal_handlers_installed = 1;
}

static void rb_restore_signal_handlers(void)
{
    if (!g_signal_handlers_installed)
        return;

    sigaction(SIGINT, &g_prev_sigint_action, NULL);
    sigaction(SIGTERM, &g_prev_sigterm_action, NULL);
    g_signal_handlers_installed = 0;
}

typedef struct {
    uint32_t flags;
} rb_sdl_init_args;

static int rb_sdl_init_video_events(uint32_t flags)
{
    const char *requested_video_driver = getenv("SDL_VIDEODRIVER");
    int try_x11_fallback = 0;

    /*
     * Desktop launchers can leave startup-notification state in the
     * environment. SDL windows created under that state may keep a busy
     * cursor over the window even after the app is responsive.
     */
    unsetenv("DESKTOP_STARTUP_ID");
    unsetenv("XDG_ACTIVATION_TOKEN");

    if (requested_video_driver == NULL || strcmp(requested_video_driver, "wayland") == 0)
        try_x11_fallback = 1;

    if (getenv("DISPLAY") && try_x11_fallback)
        setenv("SDL_VIDEODRIVER", "x11", 1);

    rb_install_x11_error_handler();
    if (SDL_Init(flags) == 0) {
        rb_install_x11_error_handler();
        return 0;
    }

    rb_install_x11_error_handler();
    if (getenv("DISPLAY") && try_x11_fallback) {
        SDL_Quit();
        setenv("SDL_VIDEODRIVER", "x11", 1);
        if (SDL_Init(flags) == 0) {
            rb_install_x11_error_handler();
            return 0;
        }
        rb_install_x11_error_handler();
    }

    return -1;
}

static int rb_sdl_init_audio_default_or_fallback(void)
{
    const char *requested_audio_driver = getenv("SDL_AUDIODRIVER");
    static const char *preferred_drivers[] = {
        "pipewire",
        "pulseaudio",
        "alsa",
        "jack",
        "sndio",
        "dsp",
        "dummy",
        "disk"
    };
    int i;
    int num_drivers;

    if (requested_audio_driver && requested_audio_driver[0] != '\0')
        return SDL_AudioInit(requested_audio_driver);

    if (SDL_AudioInit(NULL) == 0)
        return 0;

    num_drivers = SDL_GetNumAudioDrivers();
    for (i = 0; i < (int)(sizeof(preferred_drivers) / sizeof(preferred_drivers[0])); i++) {
        int j;

        for (j = 0; j < num_drivers; j++) {
            const char *driver = SDL_GetAudioDriver(j);

            if (!driver || strcmp(driver, preferred_drivers[i]) != 0)
                continue;
            if (SDL_AudioInit(driver) == 0)
                return 0;
            break;
        }
    }

    for (i = 0; i < num_drivers; i++) {
        const char *driver = SDL_GetAudioDriver(i);

        if (!driver || driver[0] == '\0')
            continue;
        if (strcmp(driver, "disk") == 0 || strcmp(driver, "dummy") == 0)
            continue;
        if (SDL_AudioInit(driver) == 0)
            return 0;
    }

    for (i = 0; i < num_drivers; i++) {
        const char *driver = SDL_GetAudioDriver(i);

        if (!driver || driver[0] == '\0')
            continue;
        if ((strcmp(driver, "disk") != 0) && (strcmp(driver, "dummy") != 0))
            continue;
        if (SDL_AudioInit(driver) == 0)
            return 0;
    }

    return -1;
}

static uintptr_t rb_sdl_init_call(void *arg)
{
    rb_sdl_init_args *a = arg;
    uint32_t base_flags = a->flags & ~(uint32_t)SDL_INIT_AUDIO;
    int want_audio = (a->flags & SDL_INIT_AUDIO) != 0;

    SDL_SetHint(SDL_HINT_NO_SIGNAL_HANDLERS, "1");

    if (rb_sdl_init_video_events(base_flags) < 0)
        return (uintptr_t)-1;

    if (want_audio && rb_sdl_init_audio_default_or_fallback() < 0) {
        SDL_Quit();
        return (uintptr_t)-1;
    }

    return 0;
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

static void *rb_event_pump_thread_main(void *arg)
{
    (void)arg;

    while (__atomic_load_n(&g_event_pump_thread_running, __ATOMIC_ACQUIRE)) {
        SDL_PumpEvents();
        SDL_Delay(1);
    }

    return NULL;
}

static uintptr_t rb_event_pump_thread_start_host_call(void *arg)
{
    (void)arg;

    if (__atomic_load_n(&g_event_pump_thread_running, __ATOMIC_ACQUIRE))
        return 1;

    __atomic_store_n(&g_event_pump_thread_running, 1, __ATOMIC_RELEASE);
    if (pthread_create(&g_event_pump_thread, NULL, rb_event_pump_thread_main, NULL) != 0) {
        __atomic_store_n(&g_event_pump_thread_running, 0, __ATOMIC_RELEASE);
        return 0;
    }

    return 1;
}

static uintptr_t rb_event_pump_thread_stop_host_call(void *arg)
{
    (void)arg;

    if (!__atomic_load_n(&g_event_pump_thread_running, __ATOMIC_ACQUIRE))
        return 0;

    __atomic_store_n(&g_event_pump_thread_running, 0, __ATOMIC_RELEASE);
    (void)pthread_join(g_event_pump_thread, NULL);
    return 0;
}

int rb_init(void)
{
    const char *requested_video_driver = getenv("SDL_VIDEODRIVER");
    const char *requested_audio_driver = getenv("SDL_AUDIODRIVER");

    if (g_initialized) {
        return 0;
    }

    rb_sdl_init_args args = { SDL_INIT_VIDEO | SDL_INIT_EVENTS | SDL_INIT_AUDIO };
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
    g_audio.buffers = NULL;
    g_audio.buffer_count = 0;
    g_shutdown_requested = 0;
    rb_install_signal_handlers();
    rb_event_install_watch();
    (void)rb_call_on_host_stack(rb_event_pump_thread_start_host_call, NULL);

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

    (void)rb_call_on_host_stack(rb_event_pump_thread_stop_host_call, NULL);
    rb_call_on_host_stack(rb_sdl_quit_call, NULL);
    rb_restore_signal_handlers();
    g_initialized = 0;
}

int rb_runtime_consume_shutdown_request(void)
{
    if (!g_shutdown_requested)
        return 0;

    g_shutdown_requested = 0;
    return 1;
}

int rb_runtime_shutdown_requested(void)
{
    return g_shutdown_requested ? 1 : 0;
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
