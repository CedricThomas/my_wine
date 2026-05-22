# winmm.dll — 15 functions

## Timer (1)

| Function | Category | SDL2 Mapping | Notes |
|----------|----------|-------------|-------|
| `timeGetTime` | critical | monotonic host clock | Pure timer API. Event pumping is handled outside the timer path. |

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
| `midiStreamOpen` | critical | custom | Initialize stream state and start worker thread lazily |
| `midiStreamOut` | critical | custom | Parse `MIDIEVENT` buffers and queue timed events |
| `midiStreamClose` | critical | custom | Stop worker, clear queue, and tear down synth backend |
| `midiStreamPause` | cosmetic | custom | Pause scheduled event dispatch |
| `midiStreamRestart` | cosmetic | custom | Resume scheduled event dispatch |
| `midiStreamProperty` | important | custom | Track `MIDIPROPTEMPO` and `MIDIPROPTIMEDIV` |
| `midiOutGetNumDevs` | critical | custom | Report availability when a readable soundfont is present |
| `midiOutPrepareHeader` | critical | WinMM bookkeeping | Maintain header flags |
| `midiOutUnprepareHeader` | critical | WinMM bookkeeping | Maintain header flags |
| `midiOutReset` | important | custom | Flush queue and stop current playback |
| `midiOutSetVolume` | cosmetic | custom | Map WinMM volume to FluidSynth gain |

Current backend:
1. WinMM stream scheduling in `winmm_doom95.c`
2. FluidSynth software synth
3. Host audio driver selection preferring `pulseaudio` on this host
