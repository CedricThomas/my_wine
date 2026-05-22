#include "dsound_types.h"
#include <stdio.h>
#include <stddef.h>
#include <string.h>
#include <unistd.h>

static int failed = 0;

#define T(cond, msg)                                                           \
    do {                                                                       \
        if (!(cond)) {                                                         \
            fprintf(stderr, "FAIL: %s\n", msg);                                \
            failed++;                                                          \
        }                                                                      \
    } while (0)

static void fill_pcm16_stereo(void *ptr, DWORD bytes)
{
    int16_t *samples = (int16_t *)ptr;
    DWORD count = bytes / sizeof(int16_t);
    for (DWORD i = 0; i + 1 < count; i += 2) {
        int16_t v = (i / 2) & 32 ? 12000 : -12000;
        samples[i] = v;
        samples[i + 1] = v;
    }
}

int main(void)
{
    LPDIRECTSOUND ds = NULL;
    LPDIRECTSOUND ds_qi = NULL;
    LPDIRECTSOUNDBUFFER primary = NULL;
    LPDIRECTSOUNDBUFFER secondary = NULL;
    WAVEFORMATEX primary_fmt;
    WAVEFORMATEX secondary_fmt;
    DSBUFFERDESC desc;
    DSBCAPS caps;
    DSCAPS ds_caps;
    void *ptr1 = NULL;
    void *ptr2 = NULL;
    DWORD bytes1 = 0;
    DWORD bytes2 = 0;
    DWORD status = 0;
    DWORD freq = 0;
    LONG volume = 0;
    LONG pan = 0;
    uint32_t ref_count;

    memset(&primary_fmt, 0, sizeof(primary_fmt));
    primary_fmt.wFormatTag = WAVE_FORMAT_PCM;
    primary_fmt.nChannels = 1;
    primary_fmt.nSamplesPerSec = 11025;
    primary_fmt.wBitsPerSample = 8;
    primary_fmt.nBlockAlign = (WORD)(primary_fmt.nChannels * (primary_fmt.wBitsPerSample / 8));
    primary_fmt.nAvgBytesPerSec = primary_fmt.nSamplesPerSec * primary_fmt.nBlockAlign;

    memset(&secondary_fmt, 0, sizeof(secondary_fmt));
    secondary_fmt.wFormatTag = WAVE_FORMAT_PCM;
    secondary_fmt.nChannels = 2;
    secondary_fmt.nSamplesPerSec = 22050;
    secondary_fmt.wBitsPerSample = 16;
    secondary_fmt.nBlockAlign = (WORD)(secondary_fmt.nChannels * (secondary_fmt.wBitsPerSample / 8));
    secondary_fmt.nAvgBytesPerSec = secondary_fmt.nSamplesPerSec * secondary_fmt.nBlockAlign;

    T(DirectSoundCreate(NULL, &ds, NULL) == DS_OK, "DirectSoundCreate failed");
    T(ds != NULL, "DirectSoundCreate returned NULL");

    if (ds) {
        T(ds->lpVtbl->QueryInterface(ds, &IID_IDirectSound, (void **)&ds_qi) == DS_OK,
          "QueryInterface(IDirectSound) failed");
        T(ds_qi == ds, "QueryInterface returned unexpected pointer");
        ref_count = ds->lpVtbl->AddRef(ds);
        T(ref_count >= 2, "AddRef returned unexpected refcount");
        T(ds->lpVtbl->Release(ds) == ref_count - 1, "Release after AddRef failed");

        memset(&ds_caps, 0, sizeof(ds_caps));
        ds_caps.dwSize = sizeof(ds_caps);
        T(ds->lpVtbl->GetCaps(ds, &ds_caps) == DS_OK, "GetCaps failed");
        T(ds_caps.dwPrimaryBuffers >= 1, "GetCaps reported no primary buffers");
        T(ds->lpVtbl->SetCooperativeLevel(ds, NULL, DSSCL_PRIORITY) == DS_OK,
          "SetCooperativeLevel failed");

        memset(&desc, 0, sizeof(desc));
        desc.dwSize = sizeof(desc);
        desc.dwFlags = DSBCAPS_PRIMARYBUFFER;
        T(ds->lpVtbl->CreateSoundBuffer(ds, &desc, (void **)&primary, NULL) == DS_OK,
          "CreateSoundBuffer(primary) failed");
        T(primary != NULL, "primary buffer is NULL");
    }

    if (primary) {
        T(primary->lpVtbl->SetFormat(primary, &primary_fmt) == DS_OK, "primary SetFormat failed");
        T(primary->lpVtbl->GetFrequency(primary, &freq) == DS_OK && freq == 11025,
          "primary GetFrequency mismatch");
    }

    if (ds) {
        memset(&desc, 0, sizeof(desc));
        desc.dwSize = sizeof(desc);
        desc.dwFlags = DSBCAPS_CTRLVOLUME | DSBCAPS_CTRLPAN | DSBCAPS_CTRLFREQUENCY;
        desc.dwBufferBytes = secondary_fmt.nAvgBytesPerSec / 20;
        desc.lpwfxFormat = &secondary_fmt;
        T(ds->lpVtbl->CreateSoundBuffer(ds, &desc, (void **)&secondary, NULL) == DS_OK,
          "CreateSoundBuffer(secondary) failed");
        T(secondary != NULL, "secondary buffer is NULL");
    }

    if (secondary) {
        LPDIRECTSOUNDBUFFER legacy_secondary = NULL;

        memset(&desc, 0, sizeof(desc));
        desc.dwSize = (DWORD)offsetof(DSBUFFERDESC, guid3DAlgorithm);
        desc.dwFlags = DSBCAPS_CTRLVOLUME | DSBCAPS_CTRLPAN | DSBCAPS_CTRLFREQUENCY;
        desc.dwBufferBytes = secondary_fmt.nAvgBytesPerSec / 20;
        desc.lpwfxFormat = &secondary_fmt;
        T(primary != NULL, "primary buffer should exist before legacy secondary test");
        primary->lpVtbl->Release(primary);
        primary = NULL;
        secondary->lpVtbl->Release(secondary);
        secondary = NULL;

        T(ds->lpVtbl->CreateSoundBuffer(ds, &desc, (void **)&legacy_secondary, NULL) == DS_OK,
          "CreateSoundBuffer(legacy secondary) failed");
        T(legacy_secondary != NULL, "legacy secondary buffer is NULL");
        secondary = legacy_secondary;
    }

    if (ds_qi) {
        ds_qi->lpVtbl->Release(ds_qi);
        ds_qi = NULL;
    }
    if (ds) {
        T(ds->lpVtbl->Release(ds) >= 1, "DirectSound release with live buffers failed");
        ds = NULL;
    }

    if (secondary) {
        memset(&caps, 0, sizeof(caps));
        caps.dwSize = sizeof(caps);
        T(secondary->lpVtbl->GetCaps(secondary, &caps) == DS_OK, "buffer GetCaps failed");
        T(caps.dwBufferBytes == secondary_fmt.nAvgBytesPerSec / 20, "buffer size mismatch");

        T(secondary->lpVtbl->Lock(secondary, 0, 0, &ptr1, &bytes1, &ptr2, &bytes2,
                                  DSBLOCK_ENTIREBUFFER) == DS_OK,
          "buffer Lock failed");
        T(ptr1 != NULL && bytes1 > 0, "buffer Lock returned empty region");
        fill_pcm16_stereo(ptr1, bytes1);
        if (ptr2 && bytes2)
            fill_pcm16_stereo(ptr2, bytes2);
        T(secondary->lpVtbl->Unlock(secondary, ptr1, bytes1, ptr2, bytes2) == DS_OK,
          "buffer Unlock failed");

        T(secondary->lpVtbl->SetVolume(secondary, -600) == DS_OK, "SetVolume failed");
        T(secondary->lpVtbl->SetPan(secondary, 1500) == DS_OK, "SetPan failed");
        T(secondary->lpVtbl->SetFrequency(secondary, 22050) == DS_OK, "SetFrequency failed");
        T(secondary->lpVtbl->SetVolume(secondary, 1) == DSERR_INVALIDPARAM,
          "SetVolume accepted out-of-range value");
        T(secondary->lpVtbl->SetPan(secondary, 10001) == DSERR_INVALIDPARAM,
          "SetPan accepted out-of-range value");
        T(secondary->lpVtbl->SetFrequency(secondary, 99) == DSERR_INVALIDPARAM,
          "SetFrequency accepted out-of-range value");
        T(secondary->lpVtbl->SetFormat(secondary, &primary_fmt) == DSERR_UNSUPPORTED,
          "secondary SetFormat accepted incompatible format");
        T(secondary->lpVtbl->GetVolume(secondary, &volume) == DS_OK && volume == -600,
          "GetVolume mismatch");
        T(secondary->lpVtbl->GetPan(secondary, &pan) == DS_OK && pan == 1500,
          "GetPan mismatch");
        T(secondary->lpVtbl->GetFrequency(secondary, &freq) == DS_OK && freq == 22050,
          "GetFrequency mismatch");
        T(secondary->lpVtbl->SetCurrentPosition(secondary, 0) == DS_OK,
          "SetCurrentPosition failed");
        T(secondary->lpVtbl->Play(secondary, 0, 0, 0) == DS_OK, "Play failed");
        T(secondary->lpVtbl->GetStatus(secondary, &status) == DS_OK &&
          (status & DSBSTATUS_PLAYING) != 0,
          "GetStatus did not report playing");
        usleep(120000);
        T(secondary->lpVtbl->GetStatus(secondary, &status) == DS_OK &&
          (status & DSBSTATUS_PLAYING) == 0,
          "GetStatus did not observe natural stop");
        T(secondary->lpVtbl->Play(secondary, 0, 0, 0) == DS_OK,
          "Play after natural stop failed");
        T(secondary->lpVtbl->GetStatus(secondary, &status) == DS_OK &&
          (status & DSBSTATUS_PLAYING) != 0,
          "Play after natural stop did not restart");
        T(secondary->lpVtbl->Stop(secondary) == DS_OK, "Stop after replay failed");
        T(secondary->lpVtbl->SetCurrentPosition(secondary, 0) == DS_OK,
          "SetCurrentPosition replay failed");
        T(secondary->lpVtbl->Play(secondary, 0, 0, DSBPLAY_LOOPING) == DS_OK,
          "looping Play failed");
        T(secondary->lpVtbl->GetStatus(secondary, &status) == DS_OK &&
          (status & DSBSTATUS_PLAYING) != 0 && (status & DSBSTATUS_LOOPING) != 0,
          "looping GetStatus mismatch");
        T(secondary->lpVtbl->Stop(secondary) == DS_OK, "Stop failed");
        T(secondary->lpVtbl->GetStatus(secondary, &status) == DS_OK &&
          (status & DSBSTATUS_PLAYING) == 0,
          "GetStatus still reported playing after Stop");
    }

    if (secondary)
        secondary->lpVtbl->Release(secondary);
    if (primary)
        primary->lpVtbl->Release(primary);

    if (failed == 0)
        printf("PASS: DirectSound test passed\n");
    else
        printf("FAIL: %d DirectSound test(s) failed\n", failed);
    return failed;
}
