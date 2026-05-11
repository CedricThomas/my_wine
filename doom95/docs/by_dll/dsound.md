# dsound.dll — 1 factory + ~26 vtable methods + custom mixer

All audio flows through `DirectSoundCreate` → `IDirectSound` vtable → `IDirectSoundBuffer` vtable.

## Factory (1)

| Function | Category | SDL2 Mapping | Notes |
|----------|----------|-------------|-------|
| `DirectSoundCreate` | critical | **Custom mock vtable** | Return `IDirectSound` interface pointer. Init SDL_AudioSpec (22050 Hz, 16-bit, stereo) on first use. |

---

## IDirectSound Vtable (12 methods)

| Method | Category | SDL2 Mapping | Notes |
|--------|----------|-------------|-------|
| `CreateSoundBuffer` | critical | Buffer in memory pool | Map WAVEFORMATEX→SDL audio format. Allocate buffer. |
| `GetCaps` | stub | Fill DSCAPS struct | Report max buffers, etc. |
| `GetCooperativeLevel` | critical | Stub | Return stored level |
| `SetCooperativeLevel` | critical | Stub | Store level (DSSCL_PRIORITY) |
| `Initialize` | stub | No-op | Return DS_OK (done in DirectSoundCreate) |
| `Compact` | stub | Stub | DS_OK |
| `DuplicateSoundBuffer` | stub | Stub | DS_OK |
| `GetSpeakerConfig` | stub | Stub | DS_OK |
| `SetSpeakerConfig` | stub | Stub | DS_OK |
| `OpenDevice` | stub | Stub | DS_OK |
| `CloseDevice` | stub | Stub | DS_OK |
| `EnumerateDevices` | stub | Stub | DS_OK |

---

## IDirectSoundBuffer Vtable (12 methods)

| Method | Category | SDL2 Mapping | Notes |
|--------|----------|-------------|-------|
| `Lock` | critical | Return pointer to buffer memory | Similar to DDraw Lock. Game writes PCM data here. |
| `Unlock` | critical | Mark buffer for mixing | Store written bytes |
| `Play` | critical | `SDL_QueueAudio()` or flag for mixing | Primary buffer: no-op. Secondary: queue/mix |
| `Stop` | critical | `SDL_ClearQueuedAudio()` or unflag | Stop mixing this buffer |
| `SetVolume` | critical | Scale buffer amplitude in mixer | Map L (-10000=mute to 0=full)→gain multiplier |
| `SetFrequency` | critical | Resample or flag | Map dwFrequency→SDL resampling. Complex; consider stubbing at 22050. |
| `SetPan` | critical | Stereo panning in mixer | Scale left/right channels |
| `SetFormat` | critical | Store WAVEFORMATEX | Update buffer format |
| `SetStatus` | critical | Set PLAYING/STOPPED flag | DSBCSTATUS_PLAYING → start mixing |
| `GetCaps` | stub | Fill DSBCAPS struct | Report buffer capabilities |
| `GetFormat` | stub | Return stored WAVEFORMATEX | |
| `GetVolume` | stub | Return stored volume | |

## Additional Methods (stub)

| Method | Category | Notes |
|--------|----------|-------|
| `GetCurrentPosition` | stub | Return 0 |
| `Restore` | stub | Return DS_OK |
| `SetLoopPoints` | stub | Store loop points |

---

## PCM Mixer (custom, ~100 lines)

```
Each frame:
  for each PLAYING buffer:
    resample if needed (buffer rate → master 22050 Hz)
    apply volume gain
    apply pan (L/R scale)
    mix into master buffer
  SDL_QueueAudio(master buffer)
```

- DOOM95 creates ~16-32 sound buffers
- Each buffer is an `IDirectSoundBuffer` with its own vtable
- `SDL_OpenAudioDevice()` called once at init with 22050 Hz 16-bit stereo
- Sample rates found in binary: **22050 Hz** (primary), **11025 Hz** (compatibility)
- Format: 16-bit stereo PCM (confirmed by nBlockAlign=4)
