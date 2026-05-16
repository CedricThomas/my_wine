/*
 * rb_audio.c
 *
 * SDL2 backend — complete audio subsystem.
 * Implements all audio functions declared in render_backend.h:
 *   device open/close, buffer lifecycle, lock/unlock, play/stop,
 *   volume/pan/frequency controls, and the SDL mixing callback.
 */

#include "rb_sdl2_priv.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>

/* ---- Per-buffer playback position (static array for SDL callback thread) ---- */
static int g_audio_pos[32];

/* ---- helper ---- */

static inline rb_audio_buf *get_audio_buf(rb_audio_buf_t buf)
{
    return (rb_audio_buf *)wine_handle_get((uint32_t)buf);
}

typedef struct {
    SDL_AudioSpec *want;
    SDL_AudioSpec *have;
} rb_sdl_open_audio_args;

static uintptr_t rb_sdl_open_audio_device_call(void *arg)
{
    rb_sdl_open_audio_args *a = arg;
    return (uintptr_t)SDL_OpenAudioDevice(
        NULL, 0, a->want, a->have,
        SDL_AUDIO_ALLOW_FREQUENCY_CHANGE |
        SDL_AUDIO_ALLOW_FORMAT_CHANGE |
        SDL_AUDIO_ALLOW_CHANNELS_CHANGE
    );
}

typedef struct {
    SDL_AudioDeviceID device;
    int pause_on;
} rb_sdl_pause_audio_args;

static uintptr_t rb_sdl_pause_audio_device_call(void *arg)
{
    rb_sdl_pause_audio_args *a = arg;
    SDL_PauseAudioDevice(a->device, a->pause_on);
    return 0;
}

static uintptr_t rb_sdl_close_audio_device_call(void *arg)
{
    SDL_CloseAudioDevice(*(SDL_AudioDeviceID *)arg);
    return 0;
}

/* ========================================================================
 * 1. rb_audio_open — open the SDL audio device
 * ======================================================================== */

int rb_audio_open(int sample_rate, int channels, int bits_per_sample,
                  int buffer_size)
{
    if (g_audio.opened)
        rb_audio_close();

    SDL_AudioSpec want, have;
    memset(&want, 0, sizeof(want));
    memset(&have, 0, sizeof(have));

    want.freq = sample_rate;
    want.channels = channels;
    want.format = (bits_per_sample == 8) ? AUDIO_U8 : AUDIO_S16SYS;
    want.silence = (bits_per_sample == 8) ? 128 : 0;
    want.samples = buffer_size;
    want.callback = rb_audio_callback;
    want.userdata = NULL;

    rb_sdl_open_audio_args open_args = { &want, &have };
    SDL_AudioDeviceID device = (SDL_AudioDeviceID)rb_call_on_host_stack(rb_sdl_open_audio_device_call, &open_args);

    if (device == 0)
        return RB_FAIL;

    g_audio.device_id = device;
    g_audio.opened = 1;
    g_audio.sample_rate = have.freq;
    g_audio.channels = have.channels;
    g_audio.bits_per_sample = (have.format == AUDIO_U8) ? 8 : 16;
    g_audio.buffer_size = have.samples;

    rb_sdl_pause_audio_args pause_args = { device, 0 };
    rb_call_on_host_stack(rb_sdl_pause_audio_device_call, &pause_args);
    return RB_OK;
}

/* ========================================================================
 * 2. rb_audio_close — close the SDL audio device
 * ======================================================================== */

void rb_audio_close(void)
{
    if (!g_audio.opened)
        return;

    /* Stop all playing buffers */
    for (int i = 0; i < g_audio_buf_count; i++) {
        if (g_audio_buffers[i])
            g_audio_buffers[i]->playing = 0;
    }

    rb_sdl_pause_audio_args pause_args = { g_audio.device_id, 1 };
    rb_call_on_host_stack(rb_sdl_pause_audio_device_call, &pause_args);
    rb_call_on_host_stack(rb_sdl_close_audio_device_call, &g_audio.device_id);

    g_audio.device_id = 0;
    g_audio.opened = 0;
}

/* ========================================================================
 * 3. rb_audio_buffer_create — allocate a DirectSound-style audio buffer
 * ======================================================================== */

rb_audio_buf_t rb_audio_buffer_create(int format, int buffer_size)
{
    if (buffer_size <= 0) {
        /* Default to one buffer's worth of raw bytes */
        buffer_size = g_audio.buffer_size * (g_audio.bits_per_sample / 8) * g_audio.channels;
    }

    rb_audio_buf *buf = rb_host_malloc(sizeof(*buf));
    if (!buf)
        return 0;

    buf->data = rb_host_calloc((size_t)buffer_size, 1);
    if (!buf->data) {
        rb_host_free(buf);
        return 0;
    }

    buf->buffer_size = buffer_size;
    buf->format = (format != 0) ? format :
                  (g_audio.bits_per_sample == 8) ? AUDIO_U8 : AUDIO_S16SYS;
    buf->playing = 0;
    buf->loop = 0;
    buf->volume = 0;            /* 0 = full volume */
    buf->pan = 0;               /* 0 = center */
    buf->frequency = g_audio.sample_rate;
    buf->gain = 1.0f;
    buf->pan_left = 0.5f;
    buf->pan_right = 0.5f;

    /* Store in the global buffer array (max 32) */
    if (g_audio_buf_count < 32) {
        g_audio_buffers[g_audio_buf_count] = buf;
        g_audio_pos[g_audio_buf_count] = 0;
        g_audio_buf_count++;
    }

    return (rb_audio_buf_t)wine_handle_alloc(HANDLE_TYPE_DS_BUFFER, buf);
}

/* ========================================================================
 * 4. rb_audio_buffer_destroy — free an audio buffer
 * ======================================================================== */

int rb_audio_buffer_destroy(rb_audio_buf_t buf)
{
    rb_audio_buf *b = get_audio_buf(buf);
    if (!b)
        return RB_FAIL;

    b->playing = 0;

    /* Remove from the global buffer array, compacting to avoid gaps */
    for (int i = 0; i < g_audio_buf_count; i++) {
        if (g_audio_buffers[i] == b) {
            g_audio_pos[i] = 0;
            /* Shift all higher entries down to close the gap */
            for (int j = i; j < g_audio_buf_count - 1; j++) {
                g_audio_buffers[j] = g_audio_buffers[j + 1];
                g_audio_pos[j] = g_audio_pos[j + 1];
            }
            g_audio_buffers[g_audio_buf_count - 1] = NULL;
            g_audio_pos[g_audio_buf_count - 1] = 0;
            g_audio_buf_count--;
            break;
        }
    }

    rb_host_free(b->data);
    rb_host_free(b);
    wine_handle_free((uint32_t)buf);
    return RB_OK;
}

/* ========================================================================
 * 5. rb_audio_buffer_lock — lock a region of the buffer for writing
 * ======================================================================== */

int rb_audio_buffer_lock(rb_audio_buf_t buf, uint32_t offset, uint32_t bytes,
                         uint8_t **out_ptr, uint32_t *out_len)
{
    rb_audio_buf *b = get_audio_buf(buf);
    if (!b || !out_ptr || !out_len)
        return RB_FAIL;

    if (offset >= (uint32_t)b->buffer_size)
        return RB_FAIL;

    if (bytes > (uint32_t)b->buffer_size - offset)
        bytes = (uint32_t)(b->buffer_size - offset);

    *out_ptr = b->data + offset;
    *out_len = bytes;
    return RB_OK;
}

/* ========================================================================
 * 6. rb_audio_buffer_unlock — unlock a buffer region (no-op for SDL)
 * ======================================================================== */

int rb_audio_buffer_unlock(rb_audio_buf_t buf, const uint8_t *ptr, uint32_t len)
{
    rb_audio_buf *b = get_audio_buf(buf);
    if (!b)
        return RB_FAIL;

    (void)ptr;
    (void)len;
    /* Mark buffer data as ready — no-op for SDL callback model */
    return RB_OK;
}

/* ========================================================================
 * 7. rb_audio_buffer_play — start (or loop) playback
 * ======================================================================== */

int rb_audio_buffer_play(rb_audio_buf_t buf, int loop)
{
    rb_audio_buf *b = get_audio_buf(buf);
    if (!b)
        return RB_FAIL;

    b->playing = 1;
    b->loop = (loop != 0);
    return RB_OK;
}

/* ========================================================================
 * 8. rb_audio_buffer_stop — stop playback
 * ======================================================================== */

int rb_audio_buffer_stop(rb_audio_buf_t buf)
{
    rb_audio_buf *b = get_audio_buf(buf);
    if (!b)
        return RB_FAIL;

    b->playing = 0;
    return RB_OK;
}

/* ========================================================================
 * 9. rb_audio_buffer_set_volume — set volume (-10000..0)
 * ======================================================================== */

int rb_audio_buffer_set_volume(rb_audio_buf_t buf, int volume)
{
    rb_audio_buf *b = get_audio_buf(buf);
    if (!b)
        return RB_FAIL;

    b->volume = volume;
    b->gain = (volume == 0) ? 1.0f : powf(10.0f, (float)volume / 6000.0f);
    return RB_OK;
}

/* ========================================================================
 * 10. rb_audio_buffer_set_pan — set pan (-10000..10000)
 * ======================================================================== */

int rb_audio_buffer_set_pan(rb_audio_buf_t buf, int pan)
{
    rb_audio_buf *b = get_audio_buf(buf);
    if (!b)
        return RB_FAIL;

    b->pan = pan;
    b->pan_left  = (10000.0f - (float)pan) / 20000.0f;  /* -10000 → 1.0 left */
    b->pan_right = (10000.0f + (float)pan) / 20000.0f;  /*  10000 → 1.0 right */
    return RB_OK;
}

/* ========================================================================
 * 11. rb_audio_buffer_set_frequency — set playback frequency
 * ======================================================================== */

int rb_audio_buffer_set_frequency(rb_audio_buf_t buf, uint32_t freq)
{
    rb_audio_buf *b = get_audio_buf(buf);
    if (!b)
        return RB_FAIL;

    b->frequency = freq;
    return RB_OK;
}

/* ========================================================================
 * 12. rb_audio_callback — SDL mixing callback (runs in audio thread)
 *
 *     Mixes all playing buffers into the output stream.
 *     Supports 16-bit stereo (Doom95 primary) and 8-bit output.
 *     Per-buffer position tracked in g_audio_pos[].
 * ======================================================================== */

void rb_audio_callback(void *userdata, uint8_t *stream, int len)
{
    (void)userdata;

    /* Start with silence */
    memset(stream, 0, (size_t)len);

    /* Device output parameters */
    int dev_bps = g_audio.bits_per_sample / 8;  /* 1 or 2 */
    int dev_ch = g_audio.channels;
    int frame_bytes = dev_bps * dev_ch;
    int num_frames = len / frame_bytes;

    for (int i = 0; i < g_audio_buf_count; i++) {
        rb_audio_buf *b = g_audio_buffers[i];
        if (!b || !b->playing)
            continue;

        /* Source buffer parameters */
        int src_bps = (b->format == AUDIO_U8) ? 1 : 2;
        int src_ch = dev_ch;  /* assume same channel count as device */
        int src_frame_bytes = src_bps * src_ch;
        int buf_frames = b->buffer_size / src_frame_bytes;

        int pos = g_audio_pos[i];

        for (int f = 0; f < num_frames; f++) {
            /* Wrap or stop when buffer is exhausted */
            if (pos >= buf_frames) {
                if (b->loop) {
                    pos = 0;
                } else {
                    b->playing = 0;
                    break;
                }
            }

            /* Read source sample(s) at current frame position */
            int16_t sample_l = 0, sample_r = 0;

            if (src_bps == 2) {
                /* 16-bit source — read directly */
                if (src_ch >= 2) {
                    sample_l  = ((int16_t *)b->data)[pos * 2 + 0];
                    sample_r  = ((int16_t *)b->data)[pos * 2 + 1];
                } else {
                    sample_l  = ((int16_t *)b->data)[pos];
                    sample_r  = sample_l;
                }
            } else {
                /* 8-bit source (unsigned, 128 = silence) — scale to 16-bit */
                if (src_ch >= 2) {
                    sample_l  = (int16_t)(((int8_t)b->data[pos * 2 + 0] - 128) * 256);
                    sample_r  = (int16_t)(((int8_t)b->data[pos * 2 + 1] - 128) * 256);
                } else {
                    sample_l  = (int16_t)(((int8_t)b->data[pos] - 128) * 256);
                    sample_r  = sample_l;
                }
            }

            /* Mix into output stream */
            if (dev_bps == 2) {
                /* 16-bit output — saturating addition into int16 */
                int16_t *out = (int16_t *)(stream + f * frame_bytes);

                int32_t mixed_l = out[0] + (int32_t)(sample_l  * b->gain * b->pan_left);
                if (mixed_l  < -32768)  mixed_l  = -32768;
                if (mixed_l  >  32767)  mixed_l  =  32767;
                out[0] = (int16_t)mixed_l;

                if (dev_ch >= 2) {
                    int32_t mixed_r = out[1] + (int32_t)(sample_r * b->gain * b->pan_right);
                    if (mixed_r  < -32768)  mixed_r  = -32768;
                    if (mixed_r  >  32767)  mixed_r  =  32767;
                    out[1] = (int16_t)mixed_r;
                }
            } else {
                /* 8-bit output — saturating addition into uint8 (128 = silence) */
                uint8_t *out = stream + f * frame_bytes;

                int32_t mixed_l = ((int8_t)(out[0] - 128))
                                + (int32_t)(sample_l * b->gain * b->pan_left / 256);
                if (mixed_l < -128)  mixed_l = -128;
                if (mixed_l >  127)  mixed_l =  127;
                out[0] = (uint8_t)(mixed_l + 128);

                if (dev_ch >= 2) {
                    int32_t mixed_r = ((int8_t)(out[1] - 128))
                                    + (int32_t)(sample_r * b->gain * b->pan_right / 256);
                    if (mixed_r < -128)  mixed_r = -128;
                    if (mixed_r >  127)  mixed_r =  127;
                    out[1] = (uint8_t)(mixed_r + 128);
                }
            }

            pos++;
        }

        g_audio_pos[i] = pos;
    }
}
