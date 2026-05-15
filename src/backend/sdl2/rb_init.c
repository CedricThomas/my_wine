/*
 * rb_init.c
 *
 * SDL2 backend initialization and shutdown.
 */

#include "rb_sdl2_priv.h"
#include <string.h>

static int g_initialized = 0;

rb_audio_state g_audio;
rb_audio_buf *g_audio_buffers[32];
int g_audio_buf_count = 0;

int rb_init(void)
{
    if (g_initialized) {
        return 0;
    }

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_EVENTS) < 0) {
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

    SDL_Quit();
    g_initialized = 0;
}
