# Audio Config

## Sound API Strategy: DirectSound + MIDI + WinMM

The binary imports both DirectSound and multiple MIDI/WinMM functions — dual audio path.

### DirectSound (primary for SFX)

- `DirectSoundCreate` → `IDirectSound`
- `IDirectSound->SetCooperativeLevel` (DSBULL_IMPORTANT/DSBULL_PRIORITY)
- `IDirectSound->CreateSoundBuffer` → primary buffer
- `IDirectSound->CreateSoundBuffer` → secondary buffers (one per sound)
- DOOM95 creates ~16-32 sound buffers

### WinMM MIDI (for music)

- `midiStreamOpen` → Open MIDI output device
- `midiStreamOut` → Play MIDI sequences
- `midiStreamPause`/`midiStreamRestart` → Pause/resume music
- `midiOutSetVolume` → Volume control
- `midiOutPrepareHeader`/`midiOutUnprepareHeader` → MIDI buffer management

### WinMM Joystick

- `joyGetNumDevs` → Count joysticks
- `joyGetDevCapsA` → Get joystick capabilities
- `joyGetPosEx` → Read joystick position

## Sample Rate

- **22050 Hz** (0x5622) found in code at multiple locations — primary sample rate for SFX
- **11025 Hz** (0x2B11) also found — possibly for lower quality/compatibility mode
- Game likely uses **16-bit stereo** PCM for DirectSound SFX (confirmed by `nBlockAlign=4` patterns)

## Sound Buffer Lock/Unlock Pattern

Error strings confirm:
```
"Sound buffer lock failure!"
"Sound buffer unlock failure!"
"Bad CreateSoundBuffer: %d on sound %d %s"
```

The game uses `Lock`/`Unlock` to write PCM data into sound buffers, then `Play()` to start playback. The format string shows the game validates buffer format:
```
"wFormatTag %d Channels %d nSamplesPerSec %d nAvgBytesPerSec %d nBlockAlign %d wBitsPerSample %d cbSize %d"
```

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

- `SDL_OpenAudioDevice()` called once at init with 22050 Hz 16-bit stereo `SDL_AudioSpec`
- Each `IDirectSoundBuffer` has its own vtable
- `Lock`/`Unlock` return pointer to buffer memory (similar to DDraw)
