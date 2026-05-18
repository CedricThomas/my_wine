# Subplan 5: DirectSound

## Status

Implemented for the maintained tree, with follow-up work still open only for
the unstable 32-bit sample path.

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
- [x] Add a compact `include/dsound_types.h`
- [x] Keep it limited to the interfaces and structs used by Doom95 or the test path

### 5.2 Implement the COM shim on top of existing backend audio
- [x] `DirectSoundCreate`
- [x] `IDirectSound::CreateSoundBuffer`
- [x] `IDirectSound::SetCooperativeLevel`
- [x] `IDirectSoundBuffer::Lock` / `Unlock`
- [x] `IDirectSoundBuffer::Play` / `Stop`
- [x] `IDirectSoundBuffer::SetVolume`
- [x] `IDirectSoundBuffer::SetFrequency`
- [x] benign stubs for the rest

### 5.3 Import-table integration
- [x] Add `dsound.dll` / `DirectSoundCreate` to `src/loader/import_table.c`

### 5.4 Verification
- [x] Add a focused buffer-playback test
- [x] Confirm the same codepath works in the 32-bit backend build and sample path
  `dsound_sample` and `dsound_sample_32` now pass in the unified sample
  scenarios on-host.

## Cleanup Notes

- Split the DirectSound implementation into a device-facing file and a
  buffer-facing file so COM lifetime rules and buffer controls are easier to
  audit.
- Replace the SDL fixed-size audio buffer array with a device-locked linked
  list that carries per-buffer format metadata.
- Keep backend defaults centralized (`22050 Hz`, stereo, `16-bit`, `4096`
  samples) instead of scattering magic numbers through the COM methods.
- Treat unsupported format mutations as explicit failures instead of silently
  accepting state the backend cannot honor.

## Scope Reduction

Defer unless traces prove otherwise:

- full device enumeration
- duplicate-buffer semantics
- advanced position reporting
- high-fidelity resampling
- a separate explicit mixer loop if `rb_audio_*` already covers the needed behavior
