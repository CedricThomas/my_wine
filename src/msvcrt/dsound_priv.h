#ifndef MY_WINE_DSOUND_PRIV_H
#define MY_WINE_DSOUND_PRIV_H

#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#include "include/debug.h"
#include "dsound_types.h"
#include "render_backend.h"
#include "wine_abi.h"

typedef struct my_ds_buffer my_ds_buffer_t;

typedef struct my_ds {
    IDirectSoundVtbl *lpVtbl;
    uint32_t ref_count;
    void *cooperative_hwnd;
    DWORD cooperative_level;
    int backend_ready;
    WAVEFORMATEX primary_format;
    my_ds_buffer_t *buffer_list;
} my_ds_t;

struct my_ds_buffer {
    IDirectSoundBufferVtbl *lpVtbl;
    uint32_t ref_count;
    my_ds_t *owner;
    my_ds_buffer_t *owner_next;
    rb_audio_buf_t rb_buffer;
    WAVEFORMATEX format;
    DWORD desc_flags;
    DWORD play_flags;
    DWORD buffer_bytes;
    LONG volume;
    LONG pan;
    DWORD frequency;
    int is_primary;
};

extern const IDirectSoundVtbl dsound_vtbl;
extern const IDirectSoundBufferVtbl dsound_buffer_vtbl;

enum {
    DSOUND_DEFAULT_SAMPLE_RATE = 22050,
    DSOUND_DEFAULT_CHANNELS = 2,
    DSOUND_DEFAULT_BITS = 16,
    DSOUND_DEFAULT_BUFFER_SAMPLES = 4096
};

void dsound_default_wave_format(WAVEFORMATEX *format);
HRESULT dsound_apply_format(my_ds_buffer_t *buffer, const WAVEFORMATEX *format);
HRESULT dsound_ensure_backend(my_ds_t *ds);
void dsound_unlink_buffer(my_ds_buffer_t *buffer);
void dsound_destroy_buffer(my_ds_buffer_t *buffer);
uint32_t dsound_ds_addref(my_ds_t *ds);
uint32_t dsound_ds_release(my_ds_t *ds);
HRESULT KERNEL32_STUB dsound_buffer_create(void *this_ptr, const DSBUFFERDESC *desc,
                                           void **out_buffer, void *outer_unknown);
HRESULT KERNEL32_STUB dsound_buffer_GetCurrentPosition(void *this_ptr,
                                                       DWORD *play_cursor,
                                                       DWORD *write_cursor);
HRESULT KERNEL32_STUB dsound_buffer_GetStatus(void *this_ptr, DWORD *status);
HRESULT KERNEL32_STUB dsound_buffer_Lock(void *this_ptr, DWORD write_cursor,
                                         DWORD write_bytes, void **ptr1,
                                         DWORD *bytes1, void **ptr2,
                                         DWORD *bytes2, DWORD flags);
HRESULT KERNEL32_STUB dsound_buffer_Play(void *this_ptr, DWORD reserved1,
                                         DWORD priority, DWORD flags);
HRESULT KERNEL32_STUB dsound_buffer_SetCurrentPosition(void *this_ptr,
                                                       DWORD new_position);
HRESULT KERNEL32_STUB dsound_buffer_SetFormat(void *this_ptr,
                                              const WAVEFORMATEX *format);
HRESULT KERNEL32_STUB dsound_buffer_SetVolume(void *this_ptr, LONG volume);
HRESULT KERNEL32_STUB dsound_buffer_SetPan(void *this_ptr, LONG pan);
HRESULT KERNEL32_STUB dsound_buffer_SetFrequency(void *this_ptr, DWORD freq);
HRESULT KERNEL32_STUB dsound_buffer_Stop(void *this_ptr);
HRESULT KERNEL32_STUB dsound_buffer_Unlock(void *this_ptr, void *ptr1,
                                           DWORD bytes1, void *ptr2,
                                           DWORD bytes2);

static inline void *dsound_alloc_mem(size_t size)
{
    return malloc(size);
}

static inline void dsound_free(void *ptr)
{
    if (!ptr)
        return;
    free(ptr);
}

#endif
