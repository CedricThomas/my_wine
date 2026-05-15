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

static int g_initialized = 0;

rb_audio_state g_audio;
rb_audio_buf *g_audio_buffers[32];
int g_audio_buf_count = 0;

typedef struct {
    uint32_t flags;
} rb_sdl_init_args;

static uintptr_t rb_sdl_init_call(void *arg)
{
    rb_sdl_init_args *a = arg;
    SDL_SetHint(SDL_HINT_NO_SIGNAL_HANDLERS, "1");
    signal(SIGINT, SIG_DFL);
    signal(SIGTERM, SIG_DFL);
    int ret = SDL_Init(a->flags);
    if (ret < 0 && getenv("DISPLAY") && !getenv("SDL_VIDEODRIVER") &&
        !getenv("MY_WINE_SAMPLE_AUTOQUIT")) {
        SDL_Quit();
        setenv("SDL_VIDEODRIVER", "x11", 1);
        ret = SDL_Init(a->flags);
    }
    if (ret < 0 && !getenv("SDL_VIDEODRIVER") &&
        getenv("MY_WINE_SAMPLE_AUTOQUIT")) {
        SDL_Quit();
        setenv("SDL_VIDEODRIVER", "dummy", 1);
        ret = SDL_Init(a->flags);
    }
    signal(SIGINT, SIG_DFL);
    signal(SIGTERM, SIG_DFL);
    return (uintptr_t)ret;
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
    if (g_initialized) {
        return 0;
    }

    rb_sdl_init_args args = { SDL_INIT_VIDEO | SDL_INIT_EVENTS };
    if ((int)rb_call_on_host_stack(rb_sdl_init_call, &args) < 0) {
        fprintf(stderr, "WARNING: SDL_Init failed: %s\n", SDL_GetError());
        return RB_FAIL;
    }

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
