# Subplan 5: DirectSound

**Goal**: Mock `IDirectSound` + `IDirectSoundBuffer` vtables (12 + 14 methods), PCM mixer.

**Outcome**: A test PE with `DirectSoundCreate → CreateSoundBuffer → Lock → write sine wave → Unlock → Play` produces audible sound.

---

## Tasks

### 5.1 dsound_types.h
- [ ] Create `include/dsound_types.h` with:
  - `HRESULT` codes (if not in ddraw_types.h)
  - `WAVEFORMATEX` struct: `wFormatTag`, `nChannels`, `nSamplesPerSec`, `nAvgBytesPerSec`, `nBlockAlign`, `wBitsPerSample`, `cbSize`
  - `DSBCAPS` flags: `DSBCAPS_CTRLFREQUENCY`, `DSBCAPS_CTRLVOLUME`, `DSBCAPS_CTLSLOOP`, `DSBCAPS_LOCHARDWARE`, `DSBCAPS_LOCPRIMARY`, `DSBCAPS_STILLPLAYING`
  - `DSBCAPS` struct: `dwCaps`, `dwReserved1`, `dwReserved2`
  - `DSBUFFERDESC` struct: `dwSize`, `dwFlags`, `dwBufferBytes`, `dwReserved`, `lpcwfxFormat`, `guid3DAlgorithm`
  - `IDirectSoundVtbl`, `IDirectSound`
  - `IDirectSoundBufferVtbl`, `IDirectSoundBuffer`
  - `DSBPLAY_LOOPING`, `DSBLOCK_ENTIREBUFFER`, `DSSCL_PRIORITY`, `DSSCL_NORMAL`

### 5.2 DirectSoundCreate + IDirectSound vtable (12 methods)
- [ ] `DirectSoundCreate()` → allocate `IDirectSound` struct with vtable, call `rb_audio_open(22050, 2, 16, 4096)`, return handle
- [ ] `QueryInterface` → stub: `E_NOTIMPL`
- [ ] `AddRef` / `Release` → stub: refcounting
- [ ] `CreateSoundBuffer` → **critical**: allocate `IDirectSoundBuffer` + PCM buffer + metadata
  - Parse `DSBUFFERDESC` for `dwBufferBytes` and `WAVEFORMATEX`
  - Return mock `IDirectSoundBuffer` with vtable
- [ ] `GetCaps` → fill `DSCAPS` struct
- [ ] `Initialize` → stub: `DS_OK`
- [ ] `Compact` → stub: `DS_OK`
- [ ] `DuplicateSoundBuffer` → stub: `DS_OK`
- [ ] `GetCooperativeLevel` → stub: `DS_OK`
- [ ] `SetCooperativeLevel` → **critical**: store cooperative level (`DSSCL_PRIORITY` for games)
- [ ] `GetSpeakerConfig` / `SetSpeakerConfig` → stub: `DS_OK`
- [ ] `OpenDevice` / `CloseDevice` → stub: `DS_OK`
- [ ] `EnumerateDevCaps` / `EnumerateDevices` → stub: `DS_OK`

### 5.3 IDirectSoundBuffer vtable (14 methods)
Each buffer gets its own `IDirectSoundBuffer` struct with this vtable:
- [ ] `QueryInterface` → stub: `E_NOTIMPL`
- [ ] `AddRef` / `Release` → stub: refcounting
- [ ] `GetCaps` → fill `DSBCAPS` struct
- [ ] `GetCurrentPosition` → stub: return 0
- [ ] `GetFormat` → return stored `WAVEFORMATEX`
- [ ] `GetVolume` → return stored volume
- [ ] `Lock` → **critical**: return PCM buffer ptr + available length
  - Handle wrap-around (two contiguous regions)
  - Store locked offset for `Unlock`
- [ ] `Unlock` → **critical**: mark buffer ready for mixing
- [ ] `Play` → **critical**: set PLAYING flag + start mixing
  - Handle `DSBPLAY_LOOPING` for looped playback
- [ ] `SetFrequency` → store target sample rate (flag resampling if differs from master 22050)
- [ ] `SetFormat` → update `WAVEFORMATEX`
- [ ] `SetVolume` → store gain multiplier (convert -10000..0 → 0.0..1.0)
- [ ] `Stop` → clear PLAYING flag
- [ ] `SetStatus` → PLAYING/STOPPED flag
- [ ] `Restore` → stub: `DS_OK`
- [ ] `SetLoopPoints` → store loop start/end

### 5.4 PCM Mixer
- [ ] Implement `dsound_mixer_frame()` (~100 lines):
  ```c
  void dsound_mixer_frame(void) {
      memset(master_buffer, 0, master_size);
      for (each PLAYING buffer) {
          if (buffer->rate != MASTER_RATE) {
              resample(buffer->data, buffer->rate, buffer->len, temp_buf, MASTER_RATE);
          } else {
              memcpy(temp_buf, buffer->data, buffer->len);
          }
          apply_gain(temp_buf, buffer->volume);  // -10000..0 → 0.0..1.0
          apply_pan(temp_buf, buffer->pan);       // -10000..10000 → L/R 0.0..1.0
          mix_add(master_buffer, temp_buf, buffer->len);
      }
      rb_audio_queue(master_buffer, master_size);
  }
  ```
- [ ] Call mixer once per game loop iteration (from `rb_event_wait` or from Flip)
- [ ] Simple linear interpolation resampler for `SetFrequency`

### 5.5 Import Table
- [ ] Add `dsound.dll` → `DirectSoundCreate` to `import_table.c`

### 5.6 Test
- [ ] Compile a test PE that:
  1. Calls `DirectSoundCreate(NULL, &ds, NULL)`
  2. Calls `ds->lpVtbl->SetCooperativeLevel(ds, hwnd, DSSCL_PRIORITY)`
  3. Creates a 1-second buffer at 22050 Hz, 16-bit stereo
  4. Locks → writes a 440 Hz sine wave → unlocks
  5. Calls `Play(0, 0, DSBPLAY_LOOPING)`
  6. `SDL_Delay(3000)`
- [ ] Expected: 440 Hz tone audible for 3 seconds

---

## Files
| File | Action |
|------|--------|
| `include/dsound_types.h` | **New** (~120 lines) |
| `src/stubs/dsound_interface.c` | **New** (~200 lines) |
| `src/stubs/dsound_buffer.c` | **New** (~400 lines) |
| `src/loader/import_table.c` | Edit: add `dsound.dll` entry |

**~720 lines, ~3 days**
