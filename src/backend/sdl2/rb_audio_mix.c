/*
 * rb_audio_mix.c
 *
 * SDL2 backend -- sample decode, playback cursor advancement, and stream
 * mixing for backend-owned audio buffers.
 */

#include "rb_sdl2_priv.h"

#include <string.h>

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

int rb_audio_frame_count(const rb_audio_buf *buf)
{
    if (!buf || buf->bytes_per_frame <= 0)
        return 0;

    return buf->buffer_size / buf->bytes_per_frame;
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

void rb_audio_mix_buffer(uint8_t *stream, int len, rb_audio_buf *buf)
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
