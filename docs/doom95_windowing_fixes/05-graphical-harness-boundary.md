# Graphical Harness Boundary

## Problem

The backend contains behavior intended only for tests, such as
`MY_WINE_SAMPLE_AUTOQUIT`. There is also pressure to compensate in runtime code
for Xvfb/xdotool-specific close behavior. That makes the production path less
representative of Doom95.

## Fix

- Keep X11/Xvfb as the deterministic graphical test environment.
- Remove test-only autoquit behavior from backend window creation.
- If a sample needs automatic exit, drive it through one of:
  - input script `windowclose`
  - posted input event from the harness
  - a sample-side timer
- Make the harness own all test driver behavior:
  - video driver selection
  - audio driver selection
  - timeouts
  - close input
  - expected exit code
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
