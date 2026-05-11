# winmm.dll — 15 functions

## Timer (1)

| Function | Category | SDL2 Mapping | Notes |
|----------|----------|-------------|-------|
| `timeGetTime` | critical | `SDL_GetTicks()` | Direct replacement. Both return milliseconds. |

## Joystick (4)

| Function | Category | SDL2 Mapping | Notes |
|----------|----------|-------------|-------|
| `joyGetNumDevs` | critical | `SDL_NumJoysticks()` | Direct map. Both return count. |
| `joyGetDevCapsA` | critical | `SDL_JoystickName()` + `SDL_JoystickNumAxes()`/`SDL_JoystickNumButtons()` | Open joystick, query caps, fill JOYCAPSA, then close |
| `joyGetPosEx` | critical | `SDL_JoystickGetAxis()` + `SDL_JoystickGetButton()` | Read axis (0-65535, mapped from SDL's -32768/+32767) and buttons. Fill JOYINFOEX. |
| `joyGetPos` | (not imported) | — | Game uses Ex variant |

**Implementation:** Maintain array of `SDL_Joystick*` pointers, opened on first `joyGetPosEx`. UWORD 0–65535 maps from Sint16 -32768–32767 via `(axis + 32768) * 2 - 1`.

## MIDI (10)

| Function | Category | SDL2 Mapping | Notes |
|----------|----------|-------------|-------|
| `midiStreamOpen` | critical | **No SDL2 equivalent** | **Custom needed.** Synth library (Timidity++, FluidSynth) or SDL_mixer. |
| `midiStreamOut` | critical | Feed MIDI events to synth | Pipe MIDI bytes to synth |
| `midiStreamClose` | critical | Close synth, free resources | Direct cleanup |
| `midiStreamPause` | cosmetic | Pause synth playback | Custom synth wrapper |
| `midiStreamRestart` | cosmetic | Resume synth | Custom |
| `midiStreamProperty` | optional | Stub (return MMSYSERR_NOERROR) | Unused |
| `midiOutGetNumDevs` | critical | Stub (return 0 or 1) | Return 1 if MIDI synth available |
| `midiOutPrepareHeader` | critical | No-op (return MMSYSERR_NOERROR) | WinMM-specific |
| `midiOutUnprepareHeader` | critical | No-op (return MMSYSERR_NOERROR) | Same |
| `midiOutReset` | optional | No-op | Return MMSYSERR_NOERROR |
| `midiOutSetVolume` | cosmetic | Stub | Adjust synth volume if available |

**MIDI has no direct SDL2 support.** Options:
1. **Stub (phase 1)** — game runs silent for music, falls back to WAV SFX
2. **libmodplug/Timidity++ (phase 2)** — full MIDI playback
3. **SDL_mixer (phase 2)** — MOD support via native drivers
