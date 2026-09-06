#include "dsound_priv.h"

static DWORD dsound_min_buffer_caps_size(void)
{
    return (DWORD)(offsetof(DSBCAPS, dwUnlockTransferRate) + sizeof(DWORD));
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

static HRESULT KERNEL32_STUB dsound_buffer_Initialize(void *this_ptr, void *direct_sound,
                                                      const DSBUFFERDESC *desc)
{
    (void)this_ptr;
    (void)direct_sound;
    (void)desc;
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
