#include "dsound_priv.h"

static DWORD dsound_legacy_buffer_desc_size(void)
{
    return (DWORD)offsetof(DSBUFFERDESC, guid3DAlgorithm);
}

static DWORD dsound_min_buffer_caps_size(void)
{
    return (DWORD)(offsetof(DSBCAPS, dwUnlockTransferRate) + sizeof(DWORD));
}

extern rb_audio_buf_t rb_audio_buffer_create(const rb_audio_format_t *format,
                                             int buffer_size) __attribute__((weak));
extern int rb_audio_buffer_destroy(rb_audio_buf_t buf) __attribute__((weak));
extern int rb_audio_buffer_lock(rb_audio_buf_t buf, uint32_t offset, uint32_t bytes,
                                uint8_t **out_ptr, uint32_t *out_len) __attribute__((weak));
extern int rb_audio_buffer_unlock(rb_audio_buf_t buf, const uint8_t *ptr,
                                  uint32_t len) __attribute__((weak));
extern int rb_audio_buffer_play(rb_audio_buf_t buf, int loop) __attribute__((weak));
extern int rb_audio_buffer_stop(rb_audio_buf_t buf) __attribute__((weak));
extern int rb_audio_buffer_set_volume(rb_audio_buf_t buf, int volume) __attribute__((weak));
extern int rb_audio_buffer_set_pan(rb_audio_buf_t buf, int pan) __attribute__((weak));
extern int rb_audio_buffer_set_frequency(rb_audio_buf_t buf, uint32_t freq) __attribute__((weak));
extern int rb_audio_buffer_set_position(rb_audio_buf_t buf, uint32_t byte_offset) __attribute__((weak));
extern int rb_audio_buffer_get_position(rb_audio_buf_t buf, uint32_t *out_byte_offset) __attribute__((weak));
extern int rb_audio_buffer_is_playing(rb_audio_buf_t buf) __attribute__((weak));
extern int rb_audio_open(int sample_rate, int channels, int bits_per_sample,
                         int buffer_size) __attribute__((weak));

static DWORD dsound_resolve_frequency(const my_ds_buffer_t *buffer, DWORD freq)
{
    if (!buffer)
        return 0;
    if (freq == DSBFREQUENCY_ORIGINAL)
        return buffer->format.nSamplesPerSec;
    return freq;
}

static HRESULT dsound_validate_control_range(LONG value, LONG min_value, LONG max_value)
{
    if (value < min_value || value > max_value)
        return DSERR_INVALIDPARAM;
    return DS_OK;
}

static HRESULT KERNEL32_STUB dsound_buffer_QueryInterface(void *this_ptr, const GUID *riid, void **ppvObj)
{
    my_ds_buffer_t *buffer = (my_ds_buffer_t *)this_ptr;

    if (!buffer || !riid || !ppvObj)
        return DSERR_INVALIDPARAM;

    *ppvObj = NULL;
    if (IsEqualGUID(riid, &IID_IUnknown) || IsEqualGUID(riid, &IID_IDirectSoundBuffer)) {
        buffer->ref_count++;
        *ppvObj = FORCE_PTR_RETURN(buffer);
        return DS_OK;
    }

    return E_NOINTERFACE;
}

static uint32_t KERNEL32_STUB dsound_buffer_AddRef(void *this_ptr)
{
    my_ds_buffer_t *buffer = (my_ds_buffer_t *)this_ptr;

    if (buffer)
        buffer->ref_count++;
    return buffer ? buffer->ref_count : 0;
}

static uint32_t KERNEL32_STUB dsound_buffer_Release(void *this_ptr)
{
    my_ds_buffer_t *buffer = (my_ds_buffer_t *)this_ptr;

    if (!buffer)
        return 0;
    if (buffer->ref_count > 0)
        buffer->ref_count--;
    if (buffer->ref_count != 0)
        return buffer->ref_count;

    dsound_destroy_buffer(buffer);
    return 0;
}

static HRESULT KERNEL32_STUB dsound_buffer_GetCaps(void *this_ptr, DSBCAPS *caps)
{
    my_ds_buffer_t *buffer = (my_ds_buffer_t *)this_ptr;
    DWORD copy_size;

    if (!buffer || !caps || caps->dwSize < dsound_min_buffer_caps_size())
        return DSERR_INVALIDPARAM;

    copy_size = caps->dwSize;
    if (copy_size > sizeof(*caps))
        copy_size = sizeof(*caps);

    memset(caps, 0, copy_size);
    caps->dwSize = sizeof(*caps);
    caps->dwFlags = buffer->desc_flags;
    caps->dwBufferBytes = buffer->buffer_bytes;
    return DS_OK;
}

static HRESULT KERNEL32_STUB dsound_buffer_GetCurrentPosition(void *this_ptr,
                                                              DWORD *play_cursor,
                                                              DWORD *write_cursor)
{
    my_ds_buffer_t *buffer = (my_ds_buffer_t *)this_ptr;
    uint32_t position = 0;

    if (!buffer)
        return DSERR_INVALIDPARAM;
    if (!buffer->is_primary && buffer->rb_buffer && rb_audio_buffer_get_position)
        rb_audio_buffer_get_position(buffer->rb_buffer, &position);
    if (play_cursor)
        *play_cursor = position;
    if (write_cursor)
        *write_cursor = position;
    return DS_OK;
}

static HRESULT KERNEL32_STUB dsound_buffer_GetFormat(void *this_ptr, WAVEFORMATEX *format,
                                                     DWORD format_size, DWORD *written)
{
    my_ds_buffer_t *buffer = (my_ds_buffer_t *)this_ptr;
    DWORD copy_size;

    if (!buffer)
        return DSERR_INVALIDPARAM;

    copy_size = sizeof(buffer->format);
    if (written)
        *written = copy_size;
    if (!format || format_size == 0)
        return DS_OK;

    if (format_size < copy_size)
        copy_size = format_size;
    memcpy(format, &buffer->format, copy_size);
    return DS_OK;
}

static HRESULT KERNEL32_STUB dsound_buffer_GetVolume(void *this_ptr, LONG *volume)
{
    my_ds_buffer_t *buffer = (my_ds_buffer_t *)this_ptr;

    if (!buffer || !volume)
        return DSERR_INVALIDPARAM;
    *volume = buffer->volume;
    return DS_OK;
}

static HRESULT KERNEL32_STUB dsound_buffer_GetPan(void *this_ptr, LONG *pan)
{
    my_ds_buffer_t *buffer = (my_ds_buffer_t *)this_ptr;

    if (!buffer || !pan)
        return DSERR_INVALIDPARAM;
    *pan = buffer->pan;
    return DS_OK;
}

static HRESULT KERNEL32_STUB dsound_buffer_GetFrequency(void *this_ptr, DWORD *freq)
{
    my_ds_buffer_t *buffer = (my_ds_buffer_t *)this_ptr;

    if (!buffer || !freq)
        return DSERR_INVALIDPARAM;
    *freq = buffer->frequency;
    return DS_OK;
}

static HRESULT KERNEL32_STUB dsound_buffer_GetStatus(void *this_ptr, DWORD *status)
{
    my_ds_buffer_t *buffer = (my_ds_buffer_t *)this_ptr;
    int playing = 0;

    if (!buffer || !status)
        return DSERR_INVALIDPARAM;

    *status = 0;
    if (buffer->is_primary)
        return DS_OK;

    if (buffer->rb_buffer && rb_audio_buffer_is_playing)
        playing = rb_audio_buffer_is_playing(buffer->rb_buffer);
    if (playing > 0)
        *status |= DSBSTATUS_PLAYING;
    if (playing > 0 && (buffer->play_flags & DSBPLAY_LOOPING))
        *status |= DSBSTATUS_LOOPING;
    return DS_OK;
}

static HRESULT KERNEL32_STUB dsound_buffer_Initialize(void *this_ptr, void *direct_sound,
                                                      const DSBUFFERDESC *desc)
{
    (void)this_ptr;
    (void)direct_sound;
    (void)desc;
    return DS_OK;
}

static HRESULT KERNEL32_STUB dsound_buffer_Lock(void *this_ptr, DWORD write_cursor,
                                                DWORD write_bytes, void **ptr1,
                                                DWORD *bytes1, void **ptr2, DWORD *bytes2,
                                                DWORD flags)
{
    my_ds_buffer_t *buffer = (my_ds_buffer_t *)this_ptr;
    static uint32_t lock_count = 0;
    DWORD offset;
    DWORD size1;
    uint8_t *data1 = NULL;
    uint8_t *data2 = NULL;

    if (!buffer || !ptr1 || !bytes1 || !ptr2 || !bytes2)
        return DSERR_INVALIDPARAM;
    if (buffer->is_primary || !buffer->rb_buffer || !rb_audio_buffer_lock)
        return DSERR_CONTROLUNAVAIL;

    *ptr1 = NULL;
    *ptr2 = NULL;
    *bytes1 = 0;
    *bytes2 = 0;

    if (buffer->buffer_bytes == 0)
        return DSERR_INVALIDPARAM;

    offset = write_cursor % buffer->buffer_bytes;
    if (flags & DSBLOCK_ENTIREBUFFER) {
        offset = 0;
        write_bytes = buffer->buffer_bytes;
    } else if (write_bytes == 0) {
        return DSERR_INVALIDPARAM;
    }

    if (offset + write_bytes <= buffer->buffer_bytes) {
        if (rb_audio_buffer_lock(buffer->rb_buffer, offset, write_bytes, &data1, bytes1) != RB_OK)
            return DSERR_INVALIDCALL;
        *ptr1 = FORCE_PTR_RETURN(data1);
        lock_count++;
        if (debug_level_at_least(1) &&
            ((lock_count & (lock_count - 1)) == 0 || lock_count <= 4u)) {
            DEBUG("dsound: Lock #%u bytes=%lu offset=%lu total=%lu primary=%d",
                  lock_count, (unsigned long)write_bytes, (unsigned long)offset,
                  (unsigned long)buffer->buffer_bytes, buffer->is_primary);
        }
        return DS_OK;
    }

    size1 = buffer->buffer_bytes - offset;
    if (rb_audio_buffer_lock(buffer->rb_buffer, offset, size1, &data1, bytes1) != RB_OK)
        return DSERR_INVALIDCALL;
    if (rb_audio_buffer_lock(buffer->rb_buffer, 0, write_bytes - size1, &data2, bytes2) != RB_OK)
        return DSERR_INVALIDCALL;

    *ptr1 = FORCE_PTR_RETURN(data1);
    *ptr2 = FORCE_PTR_RETURN(data2);
    lock_count++;
    if (debug_level_at_least(1) &&
        ((lock_count & (lock_count - 1)) == 0 || lock_count <= 4u)) {
        DEBUG("dsound: Lock #%u split bytes=%lu offset=%lu total=%lu primary=%d",
              lock_count, (unsigned long)write_bytes, (unsigned long)offset,
              (unsigned long)buffer->buffer_bytes, buffer->is_primary);
    }
    return DS_OK;
}

static HRESULT KERNEL32_STUB dsound_buffer_Play(void *this_ptr, DWORD reserved1,
                                                DWORD priority, DWORD flags)
{
    my_ds_buffer_t *buffer = (my_ds_buffer_t *)this_ptr;
    static uint32_t play_count = 0;

    (void)reserved1;
    (void)priority;

    if (!buffer)
        return DSERR_INVALIDPARAM;
    if (buffer->is_primary)
        return DS_OK;
    if (!buffer->rb_buffer || !rb_audio_buffer_play)
        return DSERR_INVALIDCALL;

    buffer->play_flags = flags;
    play_count++;
    if (debug_level_at_least(1) &&
        ((play_count & (play_count - 1)) == 0 || play_count <= 8u)) {
        DEBUG("dsound: Play #%u flags=0x%lx bytes=%lu rate=%lu channels=%u bits=%u",
              play_count, (unsigned long)flags, (unsigned long)buffer->buffer_bytes,
              (unsigned long)buffer->frequency, (unsigned)buffer->format.nChannels,
              (unsigned)buffer->format.wBitsPerSample);
    }
    if (rb_audio_buffer_play(buffer->rb_buffer, (flags & DSBPLAY_LOOPING) != 0) != RB_OK)
        return DSERR_INVALIDCALL;
    return DS_OK;
}

static HRESULT KERNEL32_STUB dsound_buffer_SetCurrentPosition(void *this_ptr, DWORD new_position)
{
    my_ds_buffer_t *buffer = (my_ds_buffer_t *)this_ptr;

    if (!buffer)
        return DSERR_INVALIDPARAM;
    if (buffer->is_primary)
        return DS_OK;
    if (!buffer->rb_buffer || !rb_audio_buffer_set_position)
        return DSERR_INVALIDCALL;
    if (rb_audio_buffer_set_position(buffer->rb_buffer, new_position) != RB_OK)
        return DSERR_INVALIDPARAM;
    return DS_OK;
}

static HRESULT KERNEL32_STUB dsound_buffer_SetFormat(void *this_ptr, const WAVEFORMATEX *format)
{
    my_ds_buffer_t *buffer = (my_ds_buffer_t *)this_ptr;
    WAVEFORMATEX old_format;
    HRESULT hr;

    if (!buffer || !format)
        return DSERR_INVALIDPARAM;

    old_format = buffer->format;
    hr = dsound_apply_format(buffer, format);
    if (hr != DS_OK)
        return hr;

    DEBUG_LEVEL(1, "dsound: SetFormat primary=%d rate=%lu channels=%u bits=%u bytes=%lu",
                buffer->is_primary, (unsigned long)format->nSamplesPerSec,
                (unsigned)format->nChannels, (unsigned)format->wBitsPerSample,
                (unsigned long)buffer->buffer_bytes);

    if (buffer->is_primary) {
        if (!rb_audio_open)
            return DSERR_UNSUPPORTED;
        if (rb_audio_open((int)format->nSamplesPerSec, (int)format->nChannels,
                          (int)format->wBitsPerSample,
                          DSOUND_DEFAULT_BUFFER_SAMPLES) != RB_OK) {
            buffer->format = old_format;
            buffer->frequency = old_format.nSamplesPerSec;
            return DSERR_INVALIDCALL;
        }
        if (buffer->owner)
            buffer->owner->primary_format = *format;
    } else {
        if (!buffer->rb_buffer || !rb_audio_buffer_set_frequency) {
            buffer->format = old_format;
            buffer->frequency = old_format.nSamplesPerSec;
            return DSERR_CONTROLUNAVAIL;
        }
        if (format->nChannels != old_format.nChannels ||
            format->wBitsPerSample != old_format.wBitsPerSample ||
            format->nBlockAlign != old_format.nBlockAlign) {
            buffer->format = old_format;
            buffer->frequency = old_format.nSamplesPerSec;
            return DSERR_UNSUPPORTED;
        }
        if (rb_audio_buffer_set_frequency(buffer->rb_buffer, format->nSamplesPerSec) != RB_OK) {
            buffer->format = old_format;
            buffer->frequency = old_format.nSamplesPerSec;
            return DSERR_INVALIDCALL;
        }
    }

    return DS_OK;
}

static HRESULT KERNEL32_STUB dsound_buffer_SetVolume(void *this_ptr, LONG volume)
{
    my_ds_buffer_t *buffer = (my_ds_buffer_t *)this_ptr;
    HRESULT hr;

    if (!buffer)
        return DSERR_INVALIDPARAM;

    hr = dsound_validate_control_range(volume, DSBVOLUME_MIN, DSBVOLUME_MAX);
    if (hr != DS_OK)
        return hr;

    buffer->volume = volume;
    if (buffer->is_primary)
        return DS_OK;
    if (!buffer->rb_buffer || !rb_audio_buffer_set_volume)
        return DSERR_CONTROLUNAVAIL;
    if (rb_audio_buffer_set_volume(buffer->rb_buffer, volume) != RB_OK)
        return DSERR_INVALIDCALL;
    return DS_OK;
}

static HRESULT KERNEL32_STUB dsound_buffer_SetPan(void *this_ptr, LONG pan)
{
    my_ds_buffer_t *buffer = (my_ds_buffer_t *)this_ptr;
    HRESULT hr;

    if (!buffer)
        return DSERR_INVALIDPARAM;

    hr = dsound_validate_control_range(pan, DSBPAN_LEFT, DSBPAN_RIGHT);
    if (hr != DS_OK)
        return hr;

    buffer->pan = pan;
    if (buffer->is_primary)
        return DS_OK;
    if (!buffer->rb_buffer || !rb_audio_buffer_set_pan)
        return DSERR_CONTROLUNAVAIL;
    if (rb_audio_buffer_set_pan(buffer->rb_buffer, pan) != RB_OK)
        return DSERR_INVALIDCALL;
    return DS_OK;
}

static HRESULT KERNEL32_STUB dsound_buffer_SetFrequency(void *this_ptr, DWORD freq)
{
    my_ds_buffer_t *buffer = (my_ds_buffer_t *)this_ptr;
    DWORD resolved_freq;

    if (!buffer)
        return DSERR_INVALIDPARAM;

    resolved_freq = dsound_resolve_frequency(buffer, freq);
    if (resolved_freq < DSBFREQUENCY_MIN || resolved_freq > DSBFREQUENCY_MAX)
        return DSERR_INVALIDPARAM;

    buffer->frequency = resolved_freq;
    if (buffer->is_primary)
        return DS_OK;
    if (!buffer->rb_buffer || !rb_audio_buffer_set_frequency)
        return DSERR_CONTROLUNAVAIL;
    if (rb_audio_buffer_set_frequency(buffer->rb_buffer, resolved_freq) != RB_OK)
        return DSERR_INVALIDCALL;
    return DS_OK;
}

static HRESULT KERNEL32_STUB dsound_buffer_Stop(void *this_ptr)
{
    my_ds_buffer_t *buffer = (my_ds_buffer_t *)this_ptr;

    if (!buffer)
        return DSERR_INVALIDPARAM;
    if (buffer->is_primary)
        return DS_OK;
    if (!buffer->rb_buffer || !rb_audio_buffer_stop)
        return DSERR_INVALIDCALL;

    buffer->play_flags = 0;
    if (rb_audio_buffer_stop(buffer->rb_buffer) != RB_OK)
        return DSERR_INVALIDCALL;
    return DS_OK;
}

static HRESULT KERNEL32_STUB dsound_buffer_Unlock(void *this_ptr, void *ptr1, DWORD bytes1,
                                                  void *ptr2, DWORD bytes2)
{
    my_ds_buffer_t *buffer = (my_ds_buffer_t *)this_ptr;

    if (!buffer)
        return DSERR_INVALIDPARAM;
    if (buffer->is_primary)
        return DS_OK;
    if (!buffer->rb_buffer || !rb_audio_buffer_unlock)
        return DSERR_INVALIDCALL;

    if (ptr1 && bytes1 && rb_audio_buffer_unlock(buffer->rb_buffer, ptr1, bytes1) != RB_OK)
        return DSERR_INVALIDCALL;
    if (ptr2 && bytes2 && rb_audio_buffer_unlock(buffer->rb_buffer, ptr2, bytes2) != RB_OK)
        return DSERR_INVALIDCALL;
    return DS_OK;
}

static HRESULT KERNEL32_STUB dsound_buffer_Restore(void *this_ptr)
{
    (void)this_ptr;
    return DS_OK;
}

const IDirectSoundBufferVtbl dsound_buffer_vtbl = {
    .QueryInterface = dsound_buffer_QueryInterface,
    .AddRef = dsound_buffer_AddRef,
    .Release = dsound_buffer_Release,
    .GetCaps = dsound_buffer_GetCaps,
    .GetCurrentPosition = dsound_buffer_GetCurrentPosition,
    .GetFormat = dsound_buffer_GetFormat,
    .GetVolume = dsound_buffer_GetVolume,
    .GetPan = dsound_buffer_GetPan,
    .GetFrequency = dsound_buffer_GetFrequency,
    .GetStatus = dsound_buffer_GetStatus,
    .Initialize = dsound_buffer_Initialize,
    .Lock = dsound_buffer_Lock,
    .Play = dsound_buffer_Play,
    .SetCurrentPosition = dsound_buffer_SetCurrentPosition,
    .SetFormat = dsound_buffer_SetFormat,
    .SetVolume = dsound_buffer_SetVolume,
    .SetPan = dsound_buffer_SetPan,
    .SetFrequency = dsound_buffer_SetFrequency,
    .Stop = dsound_buffer_Stop,
    .Unlock = dsound_buffer_Unlock,
    .Restore = dsound_buffer_Restore,
};

HRESULT KERNEL32_STUB dsound_buffer_create(void *this_ptr, const DSBUFFERDESC *desc,
                                           void **out_buffer, void *outer_unknown)
{
    my_ds_t *ds = (my_ds_t *)this_ptr;
    my_ds_buffer_t *buffer;
    HRESULT hr;
    if (!ds || !desc || !out_buffer)
        return DSERR_INVALIDPARAM;
    if (outer_unknown)
        return CLASS_E_NOAGGREGATION;
    if (desc->dwSize < dsound_legacy_buffer_desc_size())
        return DSERR_INVALIDPARAM;

    *out_buffer = NULL;
    hr = dsound_ensure_backend(ds);
    if (hr != DS_OK)
        return hr;

    buffer = dsound_alloc_mem(sizeof(*buffer));
    if (!buffer)
        return DSERR_OUTOFMEMORY;
    memset(buffer, 0, sizeof(*buffer));

    buffer->lpVtbl = (IDirectSoundBufferVtbl *)&dsound_buffer_vtbl;
    buffer->ref_count = 1;
    buffer->owner = ds;
    buffer->desc_flags = desc->dwFlags;
    buffer->buffer_bytes = desc->dwBufferBytes;
    buffer->volume = DSBVOLUME_MAX;
    buffer->pan = DSBPAN_CENTER;

    DEBUG_LEVEL(1,
                "dsound: CreateSoundBuffer flags=0x%lx bytes=%lu hasfmt=%d",
                (unsigned long)desc->dwFlags,
                (unsigned long)desc->dwBufferBytes,
                desc->lpwfxFormat != NULL);

    if (desc->dwFlags & DSBCAPS_PRIMARYBUFFER) {
        WAVEFORMATEX *primary_format = desc->lpwfxFormat ? desc->lpwfxFormat : &ds->primary_format;

        buffer->is_primary = 1;
        hr = dsound_apply_format(buffer, primary_format);
        if (hr != DS_OK) {
            dsound_free(buffer);
            return hr;
        }
    } else {
        rb_audio_format_t audio_format;

        if (!desc->lpwfxFormat || desc->dwBufferBytes == 0) {
            dsound_free(buffer);
            return DSERR_INVALIDPARAM;
        }
        hr = dsound_apply_format(buffer, desc->lpwfxFormat);
        if (hr != DS_OK) {
            dsound_free(buffer);
            return hr;
        }
        if (!rb_audio_buffer_create) {
            dsound_free(buffer);
            return DSERR_UNSUPPORTED;
        }

        audio_format.sample_rate = buffer->format.nSamplesPerSec;
        audio_format.channels = buffer->format.nChannels;
        audio_format.bits_per_sample = buffer->format.wBitsPerSample;
        buffer->rb_buffer = rb_audio_buffer_create(&audio_format, (int)desc->dwBufferBytes);
        if (!buffer->rb_buffer) {
            dsound_free(buffer);
            return DSERR_OUTOFMEMORY;
        }
        if (rb_audio_buffer_set_volume &&
            rb_audio_buffer_set_volume(buffer->rb_buffer, buffer->volume) != RB_OK) {
            rb_audio_buffer_destroy(buffer->rb_buffer);
            dsound_free(buffer);
            return DSERR_INVALIDCALL;
        }
        if (rb_audio_buffer_set_pan &&
            rb_audio_buffer_set_pan(buffer->rb_buffer, buffer->pan) != RB_OK) {
            rb_audio_buffer_destroy(buffer->rb_buffer);
            dsound_free(buffer);
            return DSERR_INVALIDCALL;
        }
        if (rb_audio_buffer_set_frequency &&
            rb_audio_buffer_set_frequency(buffer->rb_buffer, buffer->frequency) != RB_OK) {
            rb_audio_buffer_destroy(buffer->rb_buffer);
            dsound_free(buffer);
            return DSERR_INVALIDCALL;
        }
    }

    dsound_ds_addref(ds);
    buffer->owner_next = ds->buffer_list;
    ds->buffer_list = buffer;

    *out_buffer = FORCE_PTR_RETURN(buffer);
    return DS_OK;
}
