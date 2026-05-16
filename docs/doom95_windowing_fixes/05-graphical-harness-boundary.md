# Graphical Harness Boundary

## Status

Not yet implemented as of 2026-05-16.

## Current Evidence

- `bash scripts/run_samples.sh sdl2_window` currently times out with
  `FAIL  sdl2_window (close timeout)`.
- The same close-timeout behavior also occurs with the new two-window graphical
  samples under both PE32+ and PE32.
- The harness currently treats `windowclose` as an alias for `altf4`, and its
  default shutdown path after scripted input is also `Alt+F4`.

## Working Hypothesis

This section is informed by the current behavior, but it is not fully proven
yet.

The most likely issue is that the graphical harness is relying on a keyboard
shortcut as if it were a deterministic window-manager close request. Under Xvfb
that assumption appears weak: sending `Alt+F4` to the target window is not
currently producing a reliable guest-visible close path, even for the existing
single-window sample. That suggests the timeout is more likely a harness-boundary
problem than a new regression in the multi-window event-routing work.

## Problem

The backend contains behavior intended only for tests, such as
`MY_WINE_SAMPLE_AUTOQUIT`. There is also pressure to compensate in runtime code
for Xvfb/xdotool-specific close behavior. That makes the production path less
representative of Doom95.

## Fix

- Keep X11/Xvfb as the deterministic graphical test environment.
- Remove test-only autoquit behavior from backend window creation.
- If a sample needs automatic exit, drive it through one of:
  - input script `altf4`
  - posted input event from the harness
  - a sample-side timer
- Make the harness own all test driver behavior:
  - video driver selection
  - audio driver selection
  - timeouts
  - close input
  - expected exit code
- Avoid assuming `Alt+F4` is equivalent to a deterministic close-button/window-
  manager close request in Xvfb. If the harness needs a guaranteed close action,
  it should drive that explicitly instead of depending on keyboard-shortcut
  semantics.
- Treat X11 `BadWindow` as a harness compatibility issue only if it occurs from the harness close action. Prefer fixing message and destroy lifecycle first.

## Files

- `scripts/graphical_samples.sh`
- `scripts/samples.sh`
- `samples/sdl2_window/*`
- `samples/sdl2_window_32/*`
- `src/backend/sdl2/rb_window.c`
- `src/backend/sdl2/rb_init.c`

## Verification

- `bash scripts/run_samples.sh sdl2_window`
- `bash scripts/run_samples.sh sdl2_window_32`
- Manual graphical inspect still opens a real visible window under Xvfb.

## Notes

- The observations above justify prioritizing this doc next, but they do not by
  themselves prove that `Alt+F4` is the only failure mode.
- A proper implementation should separate:
  - observed failures in the current harness
  - confirmed backend/runtime bugs
  - unverified assumptions about Xvfb or xdotool behavior
