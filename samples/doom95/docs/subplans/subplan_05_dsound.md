# Subplan 5: DirectSound

## Status

Not started in the maintained tree.

## Goal

Add the thinnest DirectSound layer that reuses the existing backend audio
primitives in `render_backend.h`. Unlike the original plan, this does not need
to invent a brand-new mixer architecture unless the current backend proves
insufficient.

## Required End State

- `DirectSoundCreate` exported from `dsound.dll`
- one `IDirectSound` implementation with stable refcounting
- one `IDirectSoundBuffer` implementation sufficient for:
  - buffer creation
  - lock/unlock
  - play/stop
  - looping
  - volume/frequency setters that do not crash

## Remaining Work

### 5.1 Define only the guest-facing types we need
- [ ] Add a compact `include/dsound_types.h`
- [ ] Keep it limited to the interfaces and structs used by Doom95 or the test path

### 5.2 Implement the COM shim on top of existing backend audio
- [ ] `DirectSoundCreate`
- [ ] `IDirectSound::CreateSoundBuffer`
- [ ] `IDirectSound::SetCooperativeLevel`
- [ ] `IDirectSoundBuffer::Lock` / `Unlock`
- [ ] `IDirectSoundBuffer::Play` / `Stop`
- [ ] `IDirectSoundBuffer::SetVolume`
- [ ] `IDirectSoundBuffer::SetFrequency`
- [ ] benign stubs for the rest

### 5.3 Import-table integration
- [ ] Add `dsound.dll` / `DirectSoundCreate` to `src/loader/import_table.c`

### 5.4 Verification
- [ ] Add a focused sine-wave or buffer-playback test
- [ ] Confirm the same codepath works in the 32-bit backend build if exported there

## Scope Reduction

Defer unless traces prove otherwise:

- full device enumeration
- duplicate-buffer semantics
- advanced position reporting
- high-fidelity resampling
- a separate explicit mixer loop if `rb_audio_*` already covers the needed behavior
