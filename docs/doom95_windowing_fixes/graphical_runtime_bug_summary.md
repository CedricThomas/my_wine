# Graphical Runtime Bug Summary

## Scope

This file records the graphical runtime failures still observed after the
graphical harness boundary cleanup implemented on 2026-05-16.

The boundary cleanup is done:

- test-only `MY_WINE_SAMPLE_AUTOQUIT` hooks were removed from the SDL runtime
- direct `scripts/samples.sh run <graphical>` now dispatches to the Xvfb
  graphical harness
- `altf4` remains the only documented/public scripted close action
- `windowclose` was removed from the public harness interface

The failures below are separate runtime or sample-behavior problems.

## Current Failing Scenarios

As observed on 2026-05-16 in this workspace:

- `bash scripts/run_samples.sh sdl2_window`
  - result: `FAIL  sdl2_window (close timeout)`
- `bash scripts/run_samples.sh sdl2_two_window`
  - result: `FAIL  sdl2_two_window (close timeout)`
- `bash scripts/run_samples.sh sdl2_window_32`
  - result: process segfaults before the harness sees a window
- `bash scripts/run_samples.sh sdl2_two_window_32`
  - result: process segfaults before the harness sees a window

## Observed Behavior

### PE32+

For `sdl2_window` under `my_wine` with `GRAPHICAL_VERBOSE=1`:

- the harness sees the Xvfb root window and the expected SDL window
- the sample receives scripted input ending with `altf4`
- after the close timeout, the sample process is still running
- the guest window is still visible at timeout
- stderr only shows the SDL backend selection line:
  - `WARNING: SDL backends requested video=x11 audio=dummy active video=x11 audio=unknown`

For `sdl2_two_window` under `my_wine`:

- the scenario still ends in `close timeout`
- this happens after the harness boundary cleanup as well

### PE32

For `sdl2_window_32` and `sdl2_two_window_32` under `my_wine`:

- the process segfaults before the harness can observe a guest window
- the failure line captured by the harness is:
  - `Segmentation fault (core dumped)`
- stderr still includes the SDL backend selection line before the crash:
  - `WARNING: SDL backends requested video=x11 audio=dummy active video=x11 audio=unknown`

## Wine Reference Checks

Wine was used as a reference because the harness close-path question was in
doubt.

Observed under `GRAPHICAL_RUNTIME=wine` on 2026-05-16:

- `altf4` behaves more like the expected guest-visible close path than a raw
  X11/window-manager close request
- switching the harness default to a raw close request was not justified by the
  Wine comparison
- a Wine run of `sdl2_window` still did not produce a clean `expected=0`
  outcome in this harness session; it exited with code `1` and emitted an X11
  `BadWindow` error after the window disappeared

That means Wine helped answer the harness-boundary question, but it did not
prove that the current sample/harness/runtime combination is fully correct.

## Ruled-Out Or Backed-Out Attempts

These were tried and should not be treated as accepted fixes:

- making `windowclose` a first-class harness command and using it as the
  default shutdown action
  - backed out
  - reason: Wine reference behavior supported keeping `altf4` as the default,
    and `windowclose` is now removed from the public harness interface
- restoring or preserving backend-only autoquit behavior
  - rejected
  - reason: that would blur the harness/runtime boundary again
- changing the graphical samples to self-terminate via `SetTimer`/`KillTimer`
  - backed out
  - reason: USER32 timer support is not implemented in this runtime
- changing the graphical samples to use polling loops or direct
  `Sleep`/`DestroyWindow` shutdown
  - backed out
  - reason: those experiments did not produce a stable passing verification path

## Strongest Current Hypotheses

These are hypotheses, not confirmed root causes.

### 1. Guest-visible close/message delivery is still incomplete under X11

Evidence:

- `altf4` does not terminate the PE32+ graphical samples under `my_wine`
- the visible guest window remains present at close timeout
- the earlier event-routing and message-dispatch fixes were necessary, but may
  not be sufficient for real graphical shutdown

Possible areas:

- `WM_SYSKEYDOWN` / `WM_SYSKEYUP` / `WM_CLOSE` translation path
- active/focus targeting during scripted `Alt+F4`
- default close behavior after keyboard-driven system close
- message pump behavior under real X11/SDL timing, distinct from dummy-driver
  native tests

### 2. PE32 graphical startup still has a separate crash bug

Evidence:

- both `sdl2_window_32` and `sdl2_two_window_32` crash before the harness sees a
  window
- the crash reproduces after the harness boundary cleanup, so it is not caused
  by `MY_WINE_SAMPLE_AUTOQUIT`

Possible areas:

- PE32 callback or window-proc ABI edge cases
- PE32 host/guest context switching during SDL-backed window startup
- PE32 path differences that do not show up in dummy-driver native tests

### 3. Harness completion criteria and runtime shutdown are still decoupled

Evidence:

- earlier verbose traces showed cases where the native X11 window could
  disappear while the guest process still remained alive
- even after removing backend test hooks, the harness still depends on guest
  behavior it cannot currently force deterministically

This should be solved by fixing runtime/sample behavior, not by restoring
backend autoquit shortcuts.

## What Is Confirmed

- The harness boundary cleanup itself is implemented in code and docs.
- `MY_WINE_SAMPLE_AUTOQUIT` is no longer used by the backend runtime.
- `scripts/samples.sh` no longer relies on dummy-video autoquit behavior for
  graphical samples.
- `windowclose` is removed from the public graphical harness command set.
- `altf4` remains the documented scripted close path.
- `build/test_sdl2_backend` still passes under
  `SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy`.

## What Is Not Confirmed

- Whether `altf4` is reaching the guest exactly as intended under `my_wine`
  X11/SDL execution
- Whether the PE32+ timeout is primarily an input-routing bug, a close-message
  bug, or a message-loop/runtime-lifetime bug
- The exact PE32 crash site for the graphical samples
- Whether the samples themselves need redesign after the runtime bug is fixed

## Recommended Next Debugging Steps

1. Add targeted debug logging around SDL event translation for `Alt+F4`,
   especially `WM_SYSKEYDOWN`, `WM_SYSKEYUP`, and any synthesized `WM_CLOSE`
   path under real X11.
2. Trace the USER32 dispatch/default-close path for the graphical sample
   windows under `my_wine`, not only under dummy-driver native tests.
3. Reproduce the PE32 graphical crash with focused diagnostics before the
   harness timeout layer, so the crash site is isolated from harness behavior.
4. Keep harness policy fixed while debugging runtime behavior:
   - no backend autoquit restoration
   - no public `windowclose` command revival
   - no dummy-driver fallback for graphical scenario runs
