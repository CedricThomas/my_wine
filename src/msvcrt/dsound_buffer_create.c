#include "dsound_priv.h"

extern rb_audio_buf_t rb_audio_buffer_create(const rb_audio_format_t *format,
                                             int buffer_size) __attribute__((weak));
extern int rb_audio_buffer_destroy(rb_audio_buf_t buf) __attribute__((weak));
extern int rb_audio_buffer_set_volume(rb_audio_buf_t buf, int volume) __attribute__((weak));
extern int rb_audio_buffer_set_pan(rb_audio_buf_t buf, int pan) __attribute__((weak));
extern int rb_audio_buffer_set_frequency(rb_audio_buf_t buf, uint32_t freq) __attribute__((weak));

static DWORD dsound_legacy_buffer_desc_size(void)
{
    return (DWORD)offsetof(DSBUFFERDESC, guid3DAlgorithm);
}

static void dsound_init_buffer_defaults(my_ds_buffer_t *buffer, my_ds_t *owner,
                                        const DSBUFFERDESC *desc)
{
    memset(buffer, 0, sizeof(*buffer));
    buffer->lpVtbl = (IDirectSoundBufferVtbl *)&dsound_buffer_vtbl;
    buffer->ref_count = 1;
    buffer->owner = owner;
    buffer->desc_flags = desc->dwFlags;
    buffer->buffer_bytes = desc->dwBufferBytes;
    buffer->volume = DSBVOLUME_MAX;
    buffer->pan = DSBPAN_CENTER;
}

static HRESULT dsound_create_primary_buffer(my_ds_buffer_t *buffer, my_ds_t *owner,
                                            const DSBUFFERDESC *desc)
{
    WAVEFORMATEX *primary_format = desc->lpwfxFormat ? desc->lpwfxFormat : &owner->primary_format;

    buffer->is_primary = 1;
    return dsound_apply_format(buffer, primary_format);
}

static HRESULT dsound_configure_secondary_buffer_controls(my_ds_buffer_t *buffer)
{
    if (rb_audio_buffer_set_volume &&
        rb_audio_buffer_set_volume(buffer->rb_buffer, buffer->volume) != RB_OK)
        return DSERR_INVALIDCALL;
    if (rb_audio_buffer_set_pan &&
        rb_audio_buffer_set_pan(buffer->rb_buffer, buffer->pan) != RB_OK)
        return DSERR_INVALIDCALL;
    if (rb_audio_buffer_set_frequency &&
        rb_audio_buffer_set_frequency(buffer->rb_buffer, buffer->frequency) != RB_OK)
        return DSERR_INVALIDCALL;
    return DS_OK;
}

static HRESULT dsound_create_secondary_buffer(my_ds_buffer_t *buffer,
                                              const DSBUFFERDESC *desc)
{
    rb_audio_format_t audio_format;
    HRESULT hr;

    if (!desc->lpwfxFormat || desc->dwBufferBytes == 0)
        return DSERR_INVALIDPARAM;

    hr = dsound_apply_format(buffer, desc->lpwfxFormat);
    if (hr != DS_OK)
        return hr;
    if (!rb_audio_buffer_create)
        return DSERR_UNSUPPORTED;

    audio_format.sample_rate = buffer->format.nSamplesPerSec;
    audio_format.channels = buffer->format.nChannels;
    audio_format.bits_per_sample = buffer->format.wBitsPerSample;
    buffer->rb_buffer = rb_audio_buffer_create(&audio_format, (int)desc->dwBufferBytes);
    if (!buffer->rb_buffer)
        return DSERR_OUTOFMEMORY;

    hr = dsound_configure_secondary_buffer_controls(buffer);
    if (hr != DS_OK) {
        rb_audio_buffer_destroy(buffer->rb_buffer);
        buffer->rb_buffer = 0;
        return hr;
    }

    return DS_OK;
}

static void dsound_attach_buffer_to_owner(my_ds_t *owner, my_ds_buffer_t *buffer)
{
    dsound_ds_addref(owner);
    buffer->owner_next = owner->buffer_list;
    owner->buffer_list = buffer;
}

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

    dsound_init_buffer_defaults(buffer, ds, desc);

    DEBUG_LEVEL(1,
                "dsound: CreateSoundBuffer flags=0x%lx bytes=%lu hasfmt=%d",
                (unsigned long)desc->dwFlags,
                (unsigned long)desc->dwBufferBytes,
                desc->lpwfxFormat != NULL);

    if (desc->dwFlags & DSBCAPS_PRIMARYBUFFER)
        hr = dsound_create_primary_buffer(buffer, ds, desc);
    else
        hr = dsound_create_secondary_buffer(buffer, desc);
    if (hr != DS_OK) {
        dsound_free(buffer);
        return hr;
    }

    dsound_attach_buffer_to_owner(ds, buffer);
    *out_buffer = FORCE_PTR_RETURN(buffer);
    return DS_OK;
}
