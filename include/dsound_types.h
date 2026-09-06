#ifndef MY_WINE_DSOUND_TYPES_H
#define MY_WINE_DSOUND_TYPES_H

#include <stdint.h>
#include "ddraw_types.h"

typedef uint32_t DWORD;
typedef int32_t LONG;
typedef uint16_t WORD;
typedef uint8_t BYTE;

#ifndef E_NOINTERFACE
#define E_NOINTERFACE         ((HRESULT)0x80004002L)
#endif
#ifndef E_NOTIMPL
#define E_NOTIMPL             ((HRESULT)0x80004001L)
#endif
#ifndef E_INVALIDARG
#define E_INVALIDARG          ((HRESULT)0x80070057L)
#endif
#ifndef E_OUTOFMEMORY
#define E_OUTOFMEMORY         ((HRESULT)0x8007000EL)
#endif

#define DS_OK                 ((HRESULT)0x00000000L)
#define DSERR_CONTROLUNAVAIL  ((HRESULT)0x8878001EL)
#define DSERR_INVALIDCALL     ((HRESULT)0x88780032L)
#define DSERR_OUTOFMEMORY     E_OUTOFMEMORY
#define DSERR_INVALIDPARAM    E_INVALIDARG
#define DSERR_UNSUPPORTED     E_NOTIMPL

#define WAVE_FORMAT_PCM       0x0001

#define DSSCL_NORMAL          0x00000001L
#define DSSCL_PRIORITY        0x00000002L
#define DSSCL_EXCLUSIVE       0x00000003L
#define DSSCL_WRITEPRIMARY    0x00000004L

#define DSBCAPS_PRIMARYBUFFER 0x00000001L
#define DSBCAPS_CTRLFREQUENCY 0x00000020L
#define DSBCAPS_CTRLPAN       0x00000040L
#define DSBCAPS_CTRLVOLUME    0x00000080L

#define DSBPLAY_LOOPING       0x00000001L

#define DSBSTATUS_PLAYING     0x00000001L
#define DSBSTATUS_LOOPING     0x00000004L

#define DSBLOCK_ENTIREBUFFER  0x00000002L

#define DSBVOLUME_MIN         ((LONG)-10000)
#define DSBVOLUME_MAX         ((LONG)0)

#define DSBPAN_LEFT           ((LONG)-10000)
#define DSBPAN_CENTER         ((LONG)0)
#define DSBPAN_RIGHT          ((LONG)10000)

#define DSBFREQUENCY_ORIGINAL ((DWORD)0)
#define DSBFREQUENCY_MIN      ((DWORD)100)
#define DSBFREQUENCY_MAX      ((DWORD)200000)

static const GUID IID_IUnknown __attribute__((unused)) = {
    0x00000000, 0x0000, 0x0000,
    { 0xC0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x46 }
};

static const GUID IID_IDirectSound __attribute__((unused)) = {
    0x279AFA83, 0x4981, 0x11CE,
    { 0xA5, 0x21, 0x00, 0x20, 0xAF, 0x0B, 0xE5, 0x60 }
};

static const GUID IID_IDirectSoundBuffer __attribute__((unused)) = {
    0x279AFA85, 0x4981, 0x11CE,
    { 0xA5, 0x21, 0x00, 0x20, 0xAF, 0x0B, 0xE5, 0x60 }
};

typedef struct _WAVEFORMATEX {
    WORD  wFormatTag;
    WORD  nChannels;
    DWORD nSamplesPerSec;
    DWORD nAvgBytesPerSec;
    WORD  nBlockAlign;
    WORD  wBitsPerSample;
    WORD  cbSize;
} WAVEFORMATEX;

typedef struct _DSBUFFERDESC {
    DWORD dwSize;
    DWORD dwFlags;
    DWORD dwBufferBytes;
    DWORD dwReserved;
    WAVEFORMATEX *lpwfxFormat;
    GUID guid3DAlgorithm;
} DSBUFFERDESC;

typedef struct _DSBCAPS {
    DWORD dwSize;
    DWORD dwFlags;
    DWORD dwBufferBytes;
    DWORD dwUnlockTransferRate;
    DWORD dwPlayCpuOverhead;
} DSBCAPS;

typedef struct _DSCAPS {
    DWORD dwSize;
    DWORD dwFlags;
    DWORD dwMinSecondarySampleRate;
    DWORD dwMaxSecondarySampleRate;
    DWORD dwPrimaryBuffers;
    DWORD dwMaxHwMixingAllBuffers;
    DWORD dwMaxHwMixingStaticBuffers;
    DWORD dwMaxHwMixingStreamingBuffers;
    DWORD dwFreeHwMixingAllBuffers;
    DWORD dwFreeHwMixingStaticBuffers;
    DWORD dwFreeHwMixingStreamingBuffers;
    DWORD dwMaxHw3DAllBuffers;
    DWORD dwMaxHw3DStaticBuffers;
    DWORD dwMaxHw3DStreamingBuffers;
    DWORD dwFreeHw3DAllBuffers;
    DWORD dwFreeHw3DStaticBuffers;
    DWORD dwFreeHw3DStreamingBuffers;
    DWORD dwTotalHwMemBytes;
    DWORD dwFreeHwMemBytes;
    DWORD dwMaxContigFreeHwMemBytes;
    DWORD dwUnlockTransferRateHwBuffers;
    DWORD dwPlayCpuOverheadSwBuffers;
    DWORD dwReserved1;
    DWORD dwReserved2;
} DSCAPS;

typedef struct IDirectSoundVtbl {
    HRESULT (KERNEL32_STUB *QueryInterface)(void *this_ptr, const GUID *riid, void **ppvObj);
    uint32_t (KERNEL32_STUB *AddRef)(void *this_ptr);
    uint32_t (KERNEL32_STUB *Release)(void *this_ptr);
    HRESULT (KERNEL32_STUB *CreateSoundBuffer)(void *this_ptr, const DSBUFFERDESC *desc,
                                               void **buffer, void *outer_unknown);
    HRESULT (KERNEL32_STUB *GetCaps)(void *this_ptr, DSCAPS *caps);
    HRESULT (KERNEL32_STUB *DuplicateSoundBuffer)(void *this_ptr, void *src_buffer, void **dst_buffer);
    HRESULT (KERNEL32_STUB *SetCooperativeLevel)(void *this_ptr, void *hwnd, DWORD level);
    HRESULT (KERNEL32_STUB *Compact)(void *this_ptr);
    HRESULT (KERNEL32_STUB *GetSpeakerConfig)(void *this_ptr, DWORD *config);
    HRESULT (KERNEL32_STUB *SetSpeakerConfig)(void *this_ptr, DWORD config);
    HRESULT (KERNEL32_STUB *Initialize)(void *this_ptr, const GUID *guid);
} IDirectSoundVtbl;

typedef struct IDirectSoundBufferVtbl {
    HRESULT (KERNEL32_STUB *QueryInterface)(void *this_ptr, const GUID *riid, void **ppvObj);
    uint32_t (KERNEL32_STUB *AddRef)(void *this_ptr);
    uint32_t (KERNEL32_STUB *Release)(void *this_ptr);
    HRESULT (KERNEL32_STUB *GetCaps)(void *this_ptr, DSBCAPS *caps);
    HRESULT (KERNEL32_STUB *GetCurrentPosition)(void *this_ptr, DWORD *play_cursor, DWORD *write_cursor);
    HRESULT (KERNEL32_STUB *GetFormat)(void *this_ptr, WAVEFORMATEX *format, DWORD format_size, DWORD *written);
    HRESULT (KERNEL32_STUB *GetVolume)(void *this_ptr, LONG *volume);
    HRESULT (KERNEL32_STUB *GetPan)(void *this_ptr, LONG *pan);
    HRESULT (KERNEL32_STUB *GetFrequency)(void *this_ptr, DWORD *freq);
    HRESULT (KERNEL32_STUB *GetStatus)(void *this_ptr, DWORD *status);
    HRESULT (KERNEL32_STUB *Initialize)(void *this_ptr, void *direct_sound, const DSBUFFERDESC *desc);
    HRESULT (KERNEL32_STUB *Lock)(void *this_ptr, DWORD write_cursor, DWORD write_bytes,
                                  void **ptr1, DWORD *bytes1, void **ptr2, DWORD *bytes2, DWORD flags);
    HRESULT (KERNEL32_STUB *Play)(void *this_ptr, DWORD reserved1, DWORD priority, DWORD flags);
    HRESULT (KERNEL32_STUB *SetCurrentPosition)(void *this_ptr, DWORD new_position);
    HRESULT (KERNEL32_STUB *SetFormat)(void *this_ptr, const WAVEFORMATEX *format);
    HRESULT (KERNEL32_STUB *SetVolume)(void *this_ptr, LONG volume);
    HRESULT (KERNEL32_STUB *SetPan)(void *this_ptr, LONG pan);
    HRESULT (KERNEL32_STUB *SetFrequency)(void *this_ptr, DWORD freq);
    HRESULT (KERNEL32_STUB *Stop)(void *this_ptr);
    HRESULT (KERNEL32_STUB *Unlock)(void *this_ptr, void *ptr1, DWORD bytes1, void *ptr2, DWORD bytes2);
    HRESULT (KERNEL32_STUB *Restore)(void *this_ptr);
} IDirectSoundBufferVtbl;

typedef struct IDirectSound {
    IDirectSoundVtbl *lpVtbl;
} IDirectSound;

typedef struct IDirectSoundBuffer {
    IDirectSoundBufferVtbl *lpVtbl;
} IDirectSoundBuffer;

typedef IDirectSound *LPDIRECTSOUND;
typedef IDirectSoundBuffer *LPDIRECTSOUNDBUFFER;

HRESULT KERNEL32_STUB DirectSoundCreate(const GUID *guid, LPDIRECTSOUND *out_ds, void *outer_unknown);

#endif
