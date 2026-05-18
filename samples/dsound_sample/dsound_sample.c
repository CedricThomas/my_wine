#include <windows.h>
#include "ringingsound_pcm.h"

#define DS_OK 0
#define DSSCL_PRIORITY 2
#define DSBCAPS_PRIMARYBUFFER 0x00000001UL
#define DSBCAPS_CTRLFREQUENCY 0x00000020UL
#define DSBCAPS_CTRLVOLUME 0x00000080UL
#define DSBPLAY_LOOPING 0x00000001UL
#define DSBSTATUS_PLAYING 0x00000001UL
#define DSBLOCK_ENTIREBUFFER 0x00000002UL
#define WAVE_FORMAT_PCM 1

typedef struct _DSBUFFERDESC {
    DWORD dwSize;
    DWORD dwFlags;
    DWORD dwBufferBytes;
    DWORD dwReserved;
    WAVEFORMATEX *lpwfxFormat;
    GUID guid3DAlgorithm;
} DSBUFFERDESC;

typedef struct IDirectSoundVtbl IDirectSoundVtbl;
typedef struct IDirectSoundBufferVtbl IDirectSoundBufferVtbl;

typedef struct IDirectSound {
    IDirectSoundVtbl *lpVtbl;
} IDirectSound;

typedef struct IDirectSoundBuffer {
    IDirectSoundBufferVtbl *lpVtbl;
} IDirectSoundBuffer;

__declspec(dllimport) HRESULT WINAPI DirectSoundCreate(const GUID *, IDirectSound **, void *);

struct IDirectSoundVtbl {
    HRESULT (WINAPI *QueryInterface)(IDirectSound *, const GUID *, void **);
    ULONG (WINAPI *AddRef)(IDirectSound *);
    ULONG (WINAPI *Release)(IDirectSound *);
    HRESULT (WINAPI *CreateSoundBuffer)(IDirectSound *, const DSBUFFERDESC *, IDirectSoundBuffer **, void *);
    HRESULT (WINAPI *GetCaps)(IDirectSound *, void *);
    HRESULT (WINAPI *DuplicateSoundBuffer)(IDirectSound *, IDirectSoundBuffer *, IDirectSoundBuffer **);
    HRESULT (WINAPI *SetCooperativeLevel)(IDirectSound *, HWND, DWORD);
    HRESULT (WINAPI *Compact)(IDirectSound *);
    HRESULT (WINAPI *GetSpeakerConfig)(IDirectSound *, DWORD *);
    HRESULT (WINAPI *SetSpeakerConfig)(IDirectSound *, DWORD);
    HRESULT (WINAPI *Initialize)(IDirectSound *, const GUID *);
};

struct IDirectSoundBufferVtbl {
    HRESULT (WINAPI *QueryInterface)(IDirectSoundBuffer *, const GUID *, void **);
    ULONG (WINAPI *AddRef)(IDirectSoundBuffer *);
    ULONG (WINAPI *Release)(IDirectSoundBuffer *);
    HRESULT (WINAPI *GetCaps)(IDirectSoundBuffer *, void *);
    HRESULT (WINAPI *GetCurrentPosition)(IDirectSoundBuffer *, DWORD *, DWORD *);
    HRESULT (WINAPI *GetFormat)(IDirectSoundBuffer *, WAVEFORMATEX *, DWORD, DWORD *);
    HRESULT (WINAPI *GetVolume)(IDirectSoundBuffer *, LONG *);
    HRESULT (WINAPI *GetPan)(IDirectSoundBuffer *, LONG *);
    HRESULT (WINAPI *GetFrequency)(IDirectSoundBuffer *, DWORD *);
    HRESULT (WINAPI *GetStatus)(IDirectSoundBuffer *, DWORD *);
    HRESULT (WINAPI *Initialize)(IDirectSoundBuffer *, IDirectSound *, const DSBUFFERDESC *);
    HRESULT (WINAPI *Lock)(IDirectSoundBuffer *, DWORD, DWORD, void **, DWORD *, void **, DWORD *, DWORD);
    HRESULT (WINAPI *Play)(IDirectSoundBuffer *, DWORD, DWORD, DWORD);
    HRESULT (WINAPI *SetCurrentPosition)(IDirectSoundBuffer *, DWORD);
    HRESULT (WINAPI *SetFormat)(IDirectSoundBuffer *, const WAVEFORMATEX *);
    HRESULT (WINAPI *SetVolume)(IDirectSoundBuffer *, LONG);
    HRESULT (WINAPI *SetPan)(IDirectSoundBuffer *, LONG);
    HRESULT (WINAPI *SetFrequency)(IDirectSoundBuffer *, DWORD);
    HRESULT (WINAPI *Stop)(IDirectSoundBuffer *);
    HRESULT (WINAPI *Unlock)(IDirectSoundBuffer *, void *, DWORD, void *, DWORD);
    HRESULT (WINAPI *Restore)(IDirectSoundBuffer *);
};

static void copy_asset_pcm(void *ptr, DWORD bytes)
{
    const BYTE *src = samples_dsound_sample_assets_ringingsound_pcm;
    DWORD src_len = (DWORD)samples_dsound_sample_assets_ringingsound_pcm_len;
    BYTE *dst = (BYTE *)ptr;
    DWORD copied = 0;

    while (copied < bytes) {
        DWORD chunk = src_len;
        if (chunk > bytes - copied)
            chunk = bytes - copied;
        CopyMemory(dst + copied, src, chunk);
        copied += chunk;
    }
}

int main(void)
{
    IDirectSound *ds = NULL;
    IDirectSoundBuffer *primary = NULL;
    IDirectSoundBuffer *secondary = NULL;
    WAVEFORMATEX fmt = {0};
    DSBUFFERDESC desc = {0};
    DWORD status = 0;
    void *ptr1 = NULL;
    void *ptr2 = NULL;
    DWORD bytes1 = 0;
    DWORD bytes2 = 0;
    DWORD clip_duration_ms = 0;

    fmt.wFormatTag = WAVE_FORMAT_PCM;
    fmt.nChannels = 1;
    fmt.nSamplesPerSec = 8000;
    fmt.wBitsPerSample = 16;
    fmt.nBlockAlign = (WORD)(fmt.nChannels * (fmt.wBitsPerSample / 8));
    fmt.nAvgBytesPerSec = fmt.nSamplesPerSec * fmt.nBlockAlign;
    clip_duration_ms = ((DWORD)samples_dsound_sample_assets_ringingsound_pcm_len * 1000U) /
                       fmt.nAvgBytesPerSec;

    if (DirectSoundCreate(NULL, &ds, NULL) != DS_OK || !ds)
        return 1;
    if (ds->lpVtbl->SetCooperativeLevel(ds, NULL, DSSCL_PRIORITY) != DS_OK)
        return 1;

    desc.dwSize = sizeof(desc);
    desc.dwFlags = DSBCAPS_PRIMARYBUFFER;
    if (ds->lpVtbl->CreateSoundBuffer(ds, &desc, &primary, NULL) != DS_OK || !primary)
        return 1;
    if (primary->lpVtbl->SetFormat(primary, &fmt) != DS_OK)
        return 1;

    desc.dwFlags = DSBCAPS_CTRLVOLUME | DSBCAPS_CTRLFREQUENCY;
    desc.dwBufferBytes = (DWORD)samples_dsound_sample_assets_ringingsound_pcm_len;
    desc.lpwfxFormat = &fmt;
    if (ds->lpVtbl->CreateSoundBuffer(ds, &desc, &secondary, NULL) != DS_OK || !secondary)
        return 1;

    if (secondary->lpVtbl->Lock(secondary, 0, 0, &ptr1, &bytes1, &ptr2, &bytes2,
                                DSBLOCK_ENTIREBUFFER) != DS_OK)
        return 1;
    copy_asset_pcm(ptr1, bytes1);
    if (ptr2 && bytes2)
        copy_asset_pcm(ptr2, bytes2);
    if (secondary->lpVtbl->Unlock(secondary, ptr1, bytes1, ptr2, bytes2) != DS_OK)
        return 1;

    secondary->lpVtbl->SetVolume(secondary, -500);
    secondary->lpVtbl->SetFrequency(secondary, fmt.nSamplesPerSec);
    secondary->lpVtbl->SetCurrentPosition(secondary, 0);
#ifndef DSOUND_SAMPLE_NO_PLAYBACK
    if (secondary->lpVtbl->Play(secondary, 0, 0, 0) != DS_OK)
        return 1;
    if (secondary->lpVtbl->GetStatus(secondary, &status) != DS_OK || !(status & DSBSTATUS_PLAYING))
        return 1;

    Sleep(clip_duration_ms + 50);
    if (secondary->lpVtbl->GetStatus(secondary, &status) != DS_OK || (status & DSBSTATUS_PLAYING))
        return 1;
#endif

    secondary->lpVtbl->Release(secondary);
    primary->lpVtbl->Release(primary);
    ds->lpVtbl->Release(ds);
    ExitProcess(0);
    return 0;
}
