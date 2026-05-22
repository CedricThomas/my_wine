#include "dsound_priv.h"

extern int rb_init(void) __attribute__((weak));
extern int rb_audio_open(int sample_rate, int channels, int bits_per_sample,
                         int buffer_size) __attribute__((weak));
extern int rb_audio_buffer_destroy(rb_audio_buf_t buf) __attribute__((weak));

static DWORD dsound_min_device_caps_size(void)
{
    return (DWORD)(offsetof(DSCAPS, dwPrimaryBuffers) + sizeof(DWORD));
}

void dsound_default_wave_format(WAVEFORMATEX *format)
{
    if (!format)
        return;

    memset(format, 0, sizeof(*format));
    format->wFormatTag = WAVE_FORMAT_PCM;
    format->nChannels = DSOUND_DEFAULT_CHANNELS;
    format->nSamplesPerSec = DSOUND_DEFAULT_SAMPLE_RATE;
    format->wBitsPerSample = DSOUND_DEFAULT_BITS;
    format->nBlockAlign = (WORD)(format->nChannels * (format->wBitsPerSample / 8));
    format->nAvgBytesPerSec = format->nSamplesPerSec * format->nBlockAlign;
}

HRESULT dsound_apply_format(my_ds_buffer_t *buffer, const WAVEFORMATEX *format)
{
    WORD expected_block_align;

    if (!buffer || !format)
        return DSERR_INVALIDPARAM;
    if (format->wFormatTag != WAVE_FORMAT_PCM)
        return DSERR_UNSUPPORTED;
    if (format->nChannels == 0 || format->nSamplesPerSec == 0 || format->wBitsPerSample == 0)
        return DSERR_INVALIDPARAM;
    if (format->wBitsPerSample != 8 && format->wBitsPerSample != 16)
        return DSERR_UNSUPPORTED;

    expected_block_align = (WORD)(format->nChannels * (format->wBitsPerSample / 8));
    if (format->nBlockAlign != expected_block_align)
        return DSERR_INVALIDPARAM;
    if (format->nAvgBytesPerSec != format->nSamplesPerSec * format->nBlockAlign)
        return DSERR_INVALIDPARAM;

    buffer->format = *format;
    buffer->frequency = format->nSamplesPerSec;
    return DS_OK;
}

HRESULT dsound_ensure_backend(my_ds_t *ds)
{
    if (!ds)
        return DSERR_INVALIDPARAM;
    if (ds->backend_ready)
        return DS_OK;
    if (!rb_init || !rb_audio_open)
        return DSERR_UNSUPPORTED;
    if (rb_init() != RB_OK)
        return DSERR_INVALIDCALL;
    if (rb_audio_open((int)ds->primary_format.nSamplesPerSec,
                      (int)ds->primary_format.nChannels,
                      (int)ds->primary_format.wBitsPerSample,
                      DSOUND_DEFAULT_BUFFER_SAMPLES) != RB_OK) {
        return DSERR_INVALIDCALL;
    }

    DEBUG_LEVEL(1, "dsound: open backend rate=%lu channels=%u bits=%u",
                (unsigned long)ds->primary_format.nSamplesPerSec,
                (unsigned)ds->primary_format.nChannels,
                (unsigned)ds->primary_format.wBitsPerSample);
    ds->backend_ready = 1;
    return DS_OK;
}

void dsound_unlink_buffer(my_ds_buffer_t *buffer)
{
    my_ds_buffer_t **link;

    if (!buffer || !buffer->owner)
        return;

    link = &buffer->owner->buffer_list;
    while (*link) {
        if (*link == buffer) {
            *link = buffer->owner_next;
            break;
        }
        link = &(*link)->owner_next;
    }

    buffer->owner = NULL;
    buffer->owner_next = NULL;
}

void dsound_destroy_buffer(my_ds_buffer_t *buffer)
{
    my_ds_t *owner;

    if (!buffer)
        return;

    owner = buffer->owner;
    dsound_unlink_buffer(buffer);
    if (buffer->rb_buffer && rb_audio_buffer_destroy)
        rb_audio_buffer_destroy(buffer->rb_buffer);
    dsound_free(buffer);
    if (owner)
        dsound_ds_release(owner);
}

uint32_t dsound_ds_addref(my_ds_t *ds)
{
    if (ds)
        ds->ref_count++;
    return ds ? ds->ref_count : 0;
}

uint32_t dsound_ds_release(my_ds_t *ds)
{
    if (!ds)
        return 0;
    if (ds->ref_count > 0)
        ds->ref_count--;
    if (ds->ref_count != 0)
        return ds->ref_count;

    dsound_free(ds);
    return 0;
}

static HRESULT KERNEL32_STUB dsound_QueryInterface(void *this_ptr, const GUID *riid, void **ppvObj)
{
    my_ds_t *ds = (my_ds_t *)this_ptr;

    if (!ds || !riid || !ppvObj)
        return DSERR_INVALIDPARAM;

    *ppvObj = NULL;
    if (IsEqualGUID(riid, &IID_IUnknown) || IsEqualGUID(riid, &IID_IDirectSound)) {
        dsound_ds_addref(ds);
        *ppvObj = FORCE_PTR_RETURN(ds);
        return DS_OK;
    }

    return E_NOINTERFACE;
}

static uint32_t KERNEL32_STUB dsound_AddRef(void *this_ptr)
{
    return dsound_ds_addref((my_ds_t *)this_ptr);
}

static uint32_t KERNEL32_STUB dsound_Release(void *this_ptr)
{
    return dsound_ds_release((my_ds_t *)this_ptr);
}

static HRESULT KERNEL32_STUB dsound_GetCaps(void *this_ptr, DSCAPS *caps)
{
    my_ds_t *ds = (my_ds_t *)this_ptr;
    DWORD copy_size;

    if (!ds || !caps || caps->dwSize < dsound_min_device_caps_size())
        return DSERR_INVALIDPARAM;

    copy_size = caps->dwSize;
    if (copy_size > sizeof(*caps))
        copy_size = sizeof(*caps);

    memset(caps, 0, copy_size);
    caps->dwSize = sizeof(*caps);
    caps->dwMinSecondarySampleRate = DSBFREQUENCY_MIN;
    caps->dwMaxSecondarySampleRate = DSBFREQUENCY_MAX;
    caps->dwPrimaryBuffers = 1;
    DEBUG_LEVEL(1, "dsound: GetCaps");
    return DS_OK;
}

static HRESULT KERNEL32_STUB dsound_DuplicateSoundBuffer(void *this_ptr, void *src_buffer, void **dst_buffer)
{
    (void)this_ptr;
    (void)src_buffer;

    if (dst_buffer)
        *dst_buffer = NULL;
    return DSERR_UNSUPPORTED;
}

static HRESULT KERNEL32_STUB dsound_SetCooperativeLevel(void *this_ptr, void *hwnd, DWORD level)
{
    my_ds_t *ds = (my_ds_t *)this_ptr;

    if (!ds)
        return DSERR_INVALIDPARAM;
    if (level != DSSCL_NORMAL && level != DSSCL_PRIORITY &&
        level != DSSCL_EXCLUSIVE && level != DSSCL_WRITEPRIMARY)
        return DSERR_INVALIDPARAM;

    ds->cooperative_hwnd = hwnd;
    ds->cooperative_level = level;
    DEBUG_LEVEL(1, "dsound: SetCooperativeLevel hwnd=%p level=0x%lx", hwnd,
                (unsigned long)level);
    return DS_OK;
}

static HRESULT KERNEL32_STUB dsound_Compact(void *this_ptr)
{
    (void)this_ptr;
    return DS_OK;
}

static HRESULT KERNEL32_STUB dsound_GetSpeakerConfig(void *this_ptr, DWORD *config)
{
    (void)this_ptr;

    if (!config)
        return DSERR_INVALIDPARAM;
    *config = 0;
    return DS_OK;
}

static HRESULT KERNEL32_STUB dsound_SetSpeakerConfig(void *this_ptr, DWORD config)
{
    (void)this_ptr;
    (void)config;
    return DS_OK;
}

static HRESULT KERNEL32_STUB dsound_Initialize(void *this_ptr, const GUID *guid)
{
    (void)this_ptr;
    (void)guid;
    return DS_OK;
}

const IDirectSoundVtbl dsound_vtbl = {
    .QueryInterface = dsound_QueryInterface,
    .AddRef = dsound_AddRef,
    .Release = dsound_Release,
    .CreateSoundBuffer = dsound_buffer_create,
    .GetCaps = dsound_GetCaps,
    .DuplicateSoundBuffer = dsound_DuplicateSoundBuffer,
    .SetCooperativeLevel = dsound_SetCooperativeLevel,
    .Compact = dsound_Compact,
    .GetSpeakerConfig = dsound_GetSpeakerConfig,
    .SetSpeakerConfig = dsound_SetSpeakerConfig,
    .Initialize = dsound_Initialize,
};

HRESULT KERNEL32_STUB DirectSoundCreate(const GUID *guid, LPDIRECTSOUND *out_ds, void *outer_unknown)
{
    my_ds_t *ds;

    if (!out_ds)
        return DSERR_INVALIDPARAM;
    *out_ds = NULL;
    if (outer_unknown)
        return CLASS_E_NOAGGREGATION;
    if (guid != NULL)
        return DSERR_UNSUPPORTED;

    ds = dsound_alloc_mem(sizeof(*ds));
    if (!ds)
        return DSERR_OUTOFMEMORY;
    memset(ds, 0, sizeof(*ds));

    ds->lpVtbl = (IDirectSoundVtbl *)&dsound_vtbl;
    ds->ref_count = 1;
    ds->cooperative_level = DSSCL_NORMAL;
    dsound_default_wave_format(&ds->primary_format);

    DEBUG_LEVEL(1, "dsound: DirectSoundCreate -> %p", (void *)ds);
    *out_ds = FORCE_PTR_RETURN(ds);
    return DS_OK;
}
