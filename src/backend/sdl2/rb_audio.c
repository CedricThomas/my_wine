/*
 * rb_audio.c
 *
 * SDL2 audio backend with explicit per-buffer format metadata.
 * Buffer mutations are synchronized with the SDL audio callback through the
 * device lock, which keeps lifetime and play-state transitions predictable.
 */

#include "rb_sdl2_priv.h"
#include <math.h>
#include <string.h>

#define RB_AUDIO_CURSOR_SHIFT 32U
#define RB_AUDIO_DEFAULT_VOLUME 0
#define RB_AUDIO_DEFAULT_PAN 0

typedef struct {
    SDL_AudioDeviceID device;
} rb_sdl_audio_device_args;

typedef struct {
    SDL_AudioSpec *want;
    SDL_AudioSpec *have;
} rb_sdl_open_audio_args;

typedef struct {
    SDL_AudioDeviceID device;
    int pause_on;
} rb_sdl_pause_audio_args;

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

static uintptr_t rb_sdl_lock_audio_device_call(void *arg)
{
    rb_sdl_audio_device_args *a = arg;

    SDL_LockAudioDevice(a->device);
    return 0;
}

static uintptr_t rb_sdl_unlock_audio_device_call(void *arg)
{
    rb_sdl_audio_device_args *a = arg;

    SDL_UnlockAudioDevice(a->device);
    return 0;
}

static int rb_audio_sdl_format_from_bits(int bits_per_sample)
{
    return (bits_per_sample <= 8) ? AUDIO_U8 : AUDIO_S16SYS;
}

static int rb_audio_clamp_int(int value, int min_value, int max_value)
{
    if (value < min_value)
        return min_value;
    if (value > max_value)
        return max_value;
    return value;
}

static void rb_audio_lock_device(void)
{
    rb_sdl_audio_device_args args;

    if (!g_audio.opened || g_audio.device_id == 0)
        return;

    args.device = g_audio.device_id;
    rb_call_on_host_stack(rb_sdl_lock_audio_device_call, &args);
}

static void rb_audio_unlock_device(void)
{
    rb_sdl_audio_device_args args;

    if (!g_audio.opened || g_audio.device_id == 0)
        return;

    args.device = g_audio.device_id;
    rb_call_on_host_stack(rb_sdl_unlock_audio_device_call, &args);
}

static rb_audio_buf *rb_audio_get_buf(rb_audio_buf_t buf)
{
    return (rb_audio_buf *)wine_handle_get((uint32_t)buf);
}

static int rb_audio_frame_count(const rb_audio_buf *buf)
{
    if (!buf || buf->bytes_per_frame <= 0)
        return 0;

    return buf->buffer_size / buf->bytes_per_frame;
}

static uint64_t rb_audio_step_fp(const rb_audio_buf *buf)
{
    uint64_t source_rate;
    uint64_t device_rate;

    if (!buf)
        return 0;

    source_rate = (buf->frequency != 0) ? buf->frequency : (uint64_t)buf->sample_rate;
    device_rate = (g_audio.sample_rate > 0) ? (uint64_t)g_audio.sample_rate : 0;
    if (source_rate == 0 || device_rate == 0)
        return 0;

    return (source_rate << RB_AUDIO_CURSOR_SHIFT) / device_rate;
}

static void rb_audio_remove_buffer_locked(rb_audio_buf *buf)
{
    rb_audio_buf **link;

    if (!buf)
        return;

    link = &g_audio.buffers;
    while (*link) {
        if (*link == buf) {
            *link = buf->next;
            buf->next = NULL;
            if (g_audio.buffer_count > 0)
                g_audio.buffer_count--;
            return;
        }
        link = &(*link)->next;
    }
}

static void rb_audio_append_buffer_locked(rb_audio_buf *buf)
{
    if (!buf)
        return;

    buf->next = g_audio.buffers;
    g_audio.buffers = buf;
    g_audio.buffer_count++;
}

static void rb_audio_set_gain(rb_audio_buf *buf, int volume)
{
    if (!buf)
        return;

    buf->volume = rb_audio_clamp_int(volume, -10000, 0);
    buf->gain = (buf->volume == 0) ? 1.0f : powf(10.0f, (float)buf->volume / 6000.0f);
}

static void rb_audio_set_pan_gains(rb_audio_buf *buf, int pan)
{
    if (!buf)
        return;

    buf->pan = rb_audio_clamp_int(pan, -10000, 10000);
    if (buf->pan < 0) {
        buf->pan_left = 1.0f;
        buf->pan_right = (10000.0f + (float)buf->pan) / 10000.0f;
    } else if (buf->pan > 0) {
        buf->pan_left = (10000.0f - (float)buf->pan) / 10000.0f;
        buf->pan_right = 1.0f;
    } else {
        buf->pan_left = 1.0f;
        buf->pan_right = 1.0f;
    }
}

static int16_t rb_audio_decode_u8(uint8_t sample)
{
    return (int16_t)(((int)sample - 128) << 8);
}

static void rb_audio_read_frame(const rb_audio_buf *buf, int frame_index,
                                int16_t *out_left, int16_t *out_right)
{
    const uint8_t *src;
    int16_t left = 0;
    int16_t right = 0;

    if (!buf || !out_left || !out_right || frame_index < 0) {
        if (out_left)
            *out_left = 0;
        if (out_right)
            *out_right = 0;
        return;
    }

    src = buf->data + ((size_t)frame_index * (size_t)buf->bytes_per_frame);
    if (buf->bits_per_sample <= 8) {
        left = rb_audio_decode_u8(src[0]);
        right = (buf->channels >= 2) ? rb_audio_decode_u8(src[1]) : left;
    } else {
        const int16_t *src16 = (const int16_t *)src;

        left = src16[0];
        right = (buf->channels >= 2) ? src16[1] : left;
    }

    *out_left = left;
    *out_right = right;
}

static void rb_audio_mix_s16(uint8_t *stream, int frame_index, int frame_bytes,
                             int device_channels, const rb_audio_buf *buf,
                             int16_t left, int16_t right)
{
    int16_t *out = (int16_t *)(stream + ((size_t)frame_index * (size_t)frame_bytes));
    int32_t mixed_left;
    int32_t mixed_right;

    mixed_left = out[0] + (int32_t)((float)left * buf->gain * buf->pan_left);
    if (mixed_left < -32768)
        mixed_left = -32768;
    if (mixed_left > 32767)
        mixed_left = 32767;
    out[0] = (int16_t)mixed_left;

    if (device_channels >= 2) {
        mixed_right = out[1] + (int32_t)((float)right * buf->gain * buf->pan_right);
        if (mixed_right < -32768)
            mixed_right = -32768;
        if (mixed_right > 32767)
            mixed_right = 32767;
        out[1] = (int16_t)mixed_right;
    }
}

static void rb_audio_mix_u8(uint8_t *stream, int frame_index, int frame_bytes,
                            int device_channels, const rb_audio_buf *buf,
                            int16_t left, int16_t right)
{
    uint8_t *out = stream + ((size_t)frame_index * (size_t)frame_bytes);
    int32_t mixed_left;
    int32_t mixed_right;

    mixed_left = ((int)out[0] - 128) + (int32_t)(((float)left * buf->gain * buf->pan_left) / 256.0f);
    if (mixed_left < -128)
        mixed_left = -128;
    if (mixed_left > 127)
        mixed_left = 127;
    out[0] = (uint8_t)(mixed_left + 128);

    if (device_channels >= 2) {
        mixed_right = ((int)out[1] - 128) + (int32_t)(((float)right * buf->gain * buf->pan_right) / 256.0f);
        if (mixed_right < -128)
            mixed_right = -128;
        if (mixed_right > 127)
            mixed_right = 127;
        out[1] = (uint8_t)(mixed_right + 128);
    }
}

static void rb_audio_mix_buffer(uint8_t *stream, int len, rb_audio_buf *buf)
{
    int device_bps;
    int device_channels;
    int frame_bytes;
    int output_frames;
    int total_frames;
    uint64_t step_fp;
    int frame_index;

    if (!buf || !buf->playing || !buf->data)
        return;

    device_bps = g_audio.bits_per_sample / 8;
    device_channels = g_audio.channels;
    frame_bytes = device_bps * device_channels;
    output_frames = (frame_bytes > 0) ? (len / frame_bytes) : 0;
    total_frames = rb_audio_frame_count(buf);
    step_fp = rb_audio_step_fp(buf);
    if (output_frames <= 0 || total_frames <= 0 || step_fp == 0) {
        buf->playing = 0;
        return;
    }

    for (frame_index = 0; frame_index < output_frames; frame_index++) {
        uint64_t source_index = buf->cursor_fp >> RB_AUDIO_CURSOR_SHIFT;
        int16_t left;
        int16_t right;

        while (source_index >= (uint64_t)total_frames) {
            if (!buf->loop) {
                buf->cursor_fp = ((uint64_t)total_frames << RB_AUDIO_CURSOR_SHIFT);
                buf->playing = 0;
                return;
            }
            buf->cursor_fp -= ((uint64_t)total_frames << RB_AUDIO_CURSOR_SHIFT);
            source_index = buf->cursor_fp >> RB_AUDIO_CURSOR_SHIFT;
        }

        rb_audio_read_frame(buf, (int)source_index, &left, &right);
        if (device_bps == 1) {
            rb_audio_mix_u8(stream, frame_index, frame_bytes, device_channels, buf, left, right);
        } else {
            rb_audio_mix_s16(stream, frame_index, frame_bytes, device_channels, buf, left, right);
        }
        buf->cursor_fp += step_fp;
    }
}

static void rb_audio_stop_all_locked(void)
{
    rb_audio_buf *buf;

    for (buf = g_audio.buffers; buf; buf = buf->next)
        buf->playing = 0;
}

int rb_audio_open(int sample_rate, int channels, int bits_per_sample,
                  int buffer_size)
{
    SDL_AudioSpec want;
    SDL_AudioSpec have;
    rb_sdl_open_audio_args open_args;
    rb_sdl_pause_audio_args pause_args;
    SDL_AudioDeviceID device;

    if (g_audio.opened)
        rb_audio_close();

    memset(&want, 0, sizeof(want));
    memset(&have, 0, sizeof(have));

    want.freq = sample_rate;
    want.channels = channels;
    want.format = rb_audio_sdl_format_from_bits(bits_per_sample);
    want.silence = (bits_per_sample <= 8) ? 128 : 0;
    want.samples = buffer_size;
    want.callback = rb_audio_callback;
    want.userdata = NULL;

    open_args.want = &want;
    open_args.have = &have;
    device = (SDL_AudioDeviceID)rb_call_on_host_stack(rb_sdl_open_audio_device_call, &open_args);
    if (device == 0)
        return RB_FAIL;

    g_audio.device_id = device;
    g_audio.opened = 1;
    g_audio.sample_rate = have.freq;
    g_audio.channels = have.channels;
    g_audio.bits_per_sample = (have.format == AUDIO_U8) ? 8 : 16;
    g_audio.buffer_size = have.samples;

    pause_args.device = device;
    pause_args.pause_on = 0;
    rb_call_on_host_stack(rb_sdl_pause_audio_device_call, &pause_args);
    return RB_OK;
}

void rb_audio_close(void)
{
    rb_sdl_pause_audio_args pause_args;

    if (!g_audio.opened)
        return;

    rb_audio_lock_device();
    rb_audio_stop_all_locked();
    rb_audio_unlock_device();

    pause_args.device = g_audio.device_id;
    pause_args.pause_on = 1;
    rb_call_on_host_stack(rb_sdl_pause_audio_device_call, &pause_args);
    rb_call_on_host_stack(rb_sdl_close_audio_device_call, &g_audio.device_id);

    g_audio.device_id = 0;
    g_audio.opened = 0;
}

rb_audio_buf_t rb_audio_buffer_create(const rb_audio_format_t *format, int buffer_size)
{
    rb_audio_buf *buf;
    rb_audio_format_t effective_format;
    rb_audio_buf_t handle;

    effective_format.sample_rate = (format && format->sample_rate != 0)
        ? format->sample_rate : (uint32_t)g_audio.sample_rate;
    effective_format.channels = (format && format->channels != 0)
        ? format->channels : (uint16_t)g_audio.channels;
    effective_format.bits_per_sample = (format && format->bits_per_sample != 0)
        ? format->bits_per_sample : (uint16_t)g_audio.bits_per_sample;

    if (effective_format.channels == 0 || effective_format.bits_per_sample == 0)
        return 0;
    if (effective_format.bits_per_sample != 8 && effective_format.bits_per_sample != 16)
        return 0;

    if (buffer_size <= 0) {
        buffer_size = g_audio.buffer_size *
            (effective_format.bits_per_sample / 8) *
            effective_format.channels;
    }

    buf = rb_host_malloc(sizeof(*buf));
    if (!buf)
        return 0;
    memset(buf, 0, sizeof(*buf));

    buf->data = rb_host_calloc((size_t)buffer_size, 1);
    if (!buf->data) {
        rb_host_free(buf);
        return 0;
    }

    buf->buffer_size = buffer_size;
    buf->sample_rate = (int)effective_format.sample_rate;
    buf->channels = effective_format.channels;
    buf->bits_per_sample = effective_format.bits_per_sample;
    buf->bytes_per_frame = (buf->bits_per_sample / 8) * buf->channels;
    buf->frequency = effective_format.sample_rate;
    rb_audio_set_gain(buf, RB_AUDIO_DEFAULT_VOLUME);
    rb_audio_set_pan_gains(buf, RB_AUDIO_DEFAULT_PAN);

    handle = (rb_audio_buf_t)wine_handle_alloc(HANDLE_TYPE_DS_BUFFER, buf);
    if (!handle) {
        rb_host_free(buf->data);
        rb_host_free(buf);
        return 0;
    }

    rb_audio_lock_device();
    rb_audio_append_buffer_locked(buf);
    rb_audio_unlock_device();
    return handle;
}

int rb_audio_buffer_destroy(rb_audio_buf_t buf)
{
    rb_audio_buf *audio_buf = rb_audio_get_buf(buf);

    if (!audio_buf)
        return RB_FAIL;

    rb_audio_lock_device();
    audio_buf->playing = 0;
    rb_audio_remove_buffer_locked(audio_buf);
    rb_audio_unlock_device();

    rb_host_free(audio_buf->data);
    rb_host_free(audio_buf);
    wine_handle_free((uint32_t)buf);
    return RB_OK;
}

int rb_audio_buffer_lock(rb_audio_buf_t buf, uint32_t offset, uint32_t bytes,
                         uint8_t **out_ptr, uint32_t *out_len)
{
    rb_audio_buf *audio_buf = rb_audio_get_buf(buf);

    if (!audio_buf || !out_ptr || !out_len)
        return RB_FAIL;
    if (offset >= (uint32_t)audio_buf->buffer_size)
        return RB_FAIL;

    if (bytes > (uint32_t)audio_buf->buffer_size - offset)
        bytes = (uint32_t)(audio_buf->buffer_size - offset);

    *out_ptr = audio_buf->data + offset;
    *out_len = bytes;
    return RB_OK;
}

int rb_audio_buffer_unlock(rb_audio_buf_t buf, const uint8_t *ptr, uint32_t len)
{
    rb_audio_buf *audio_buf = rb_audio_get_buf(buf);

    if (!audio_buf)
        return RB_FAIL;

    (void)ptr;
    (void)len;
    return RB_OK;
}

int rb_audio_buffer_play(rb_audio_buf_t buf, int loop)
{
    rb_audio_buf *audio_buf = rb_audio_get_buf(buf);

    if (!audio_buf)
        return RB_FAIL;

    rb_audio_lock_device();
    audio_buf->playing = 1;
    audio_buf->loop = (loop != 0);
    rb_audio_unlock_device();
    return RB_OK;
}

int rb_audio_buffer_stop(rb_audio_buf_t buf)
{
    rb_audio_buf *audio_buf = rb_audio_get_buf(buf);

    if (!audio_buf)
        return RB_FAIL;

    rb_audio_lock_device();
    audio_buf->playing = 0;
    rb_audio_unlock_device();
    return RB_OK;
}

int rb_audio_buffer_set_volume(rb_audio_buf_t buf, int volume)
{
    rb_audio_buf *audio_buf = rb_audio_get_buf(buf);

    if (!audio_buf)
        return RB_FAIL;

    rb_audio_lock_device();
    rb_audio_set_gain(audio_buf, volume);
    rb_audio_unlock_device();
    return RB_OK;
}

int rb_audio_buffer_set_pan(rb_audio_buf_t buf, int pan)
{
    rb_audio_buf *audio_buf = rb_audio_get_buf(buf);

    if (!audio_buf)
        return RB_FAIL;

    rb_audio_lock_device();
    rb_audio_set_pan_gains(audio_buf, pan);
    rb_audio_unlock_device();
    return RB_OK;
}

int rb_audio_buffer_set_frequency(rb_audio_buf_t buf, uint32_t freq)
{
    rb_audio_buf *audio_buf = rb_audio_get_buf(buf);

    if (!audio_buf || freq == 0)
        return RB_FAIL;

    rb_audio_lock_device();
    audio_buf->frequency = freq;
    rb_audio_unlock_device();
    return RB_OK;
}

int rb_audio_buffer_set_position(rb_audio_buf_t buf, uint32_t byte_offset)
{
    rb_audio_buf *audio_buf = rb_audio_get_buf(buf);
    int max_frames;
    uint32_t frame_index;

    if (!audio_buf || audio_buf->bytes_per_frame <= 0)
        return RB_FAIL;

    max_frames = rb_audio_frame_count(audio_buf);
    if (byte_offset > (uint32_t)audio_buf->buffer_size)
        byte_offset = (uint32_t)audio_buf->buffer_size;

    frame_index = byte_offset / (uint32_t)audio_buf->bytes_per_frame;
    if ((int)frame_index > max_frames)
        frame_index = (uint32_t)max_frames;

    rb_audio_lock_device();
    audio_buf->cursor_fp = ((uint64_t)frame_index << RB_AUDIO_CURSOR_SHIFT);
    rb_audio_unlock_device();
    return RB_OK;
}

int rb_audio_buffer_get_position(rb_audio_buf_t buf, uint32_t *out_byte_offset)
{
    rb_audio_buf *audio_buf = rb_audio_get_buf(buf);
    uint64_t cursor_fp;

    if (!audio_buf || !out_byte_offset || audio_buf->bytes_per_frame <= 0)
        return RB_FAIL;

    rb_audio_lock_device();
    cursor_fp = audio_buf->cursor_fp;
    rb_audio_unlock_device();

    *out_byte_offset = (uint32_t)((cursor_fp >> RB_AUDIO_CURSOR_SHIFT) *
                                  (uint64_t)audio_buf->bytes_per_frame);
    return RB_OK;
}

int rb_audio_buffer_is_playing(rb_audio_buf_t buf)
{
    rb_audio_buf *audio_buf = rb_audio_get_buf(buf);
    int playing;

    if (!audio_buf)
        return RB_FAIL;
    if (rb_runtime_shutdown_requested())
        return 0;

    rb_audio_lock_device();
    playing = audio_buf->playing;
    rb_audio_unlock_device();
    return playing ? 1 : 0;
}

void rb_audio_callback(void *userdata, uint8_t *stream, int len)
{
    rb_audio_buf *buf;

    (void)userdata;

    if (g_audio.bits_per_sample <= 8) {
        memset(stream, 128, (size_t)len);
    } else {
        memset(stream, 0, (size_t)len);
    }

    if (rb_runtime_shutdown_requested()) {
        rb_audio_stop_all_locked();
        return;
    }

    for (buf = g_audio.buffers; buf; buf = buf->next)
        rb_audio_mix_buffer(stream, len, buf);
}
