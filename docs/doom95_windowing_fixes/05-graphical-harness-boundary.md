# Graphical Harness Boundary

## Status

Implemented on 2026-05-16.

The harness now owns graphical test-driver behavior explicitly. Test-only
`MY_WINE_SAMPLE_AUTOQUIT` handling has been removed from the SDL runtime path,
and direct `scripts/samples.sh run <graphical-sample>` invocations dispatch to
the Xvfb graphical harness instead of depending on backend-only autoquit logic.
The default scripted shutdown path remains `altf4`, because a Wine reference
run behaves more like a guest-visible close under `Alt+F4` than under a raw
X11/window-manager close request. WM-driven `closewindow` remains an
experimental harness action for follow-up investigation, not a trusted default
or public reference path.

## Problem

The backend had behavior intended only for samples and tests:

- `MY_WINE_SAMPLE_AUTOQUIT` could inject `SDL_QUIT` during window creation.
- SDL initialization could fall back to the dummy video driver only when that
  test-only env var was set.
- The graphical harness mixed close-path experiments into its public input
  language instead of keeping one documented default close path.

That mixed test-driver policy into production runtime code and made the harness
depend on keyboard-shortcut semantics when it really needed an explicit close
request boundary.

## Implemented

- Removed test-only `MY_WINE_SAMPLE_AUTOQUIT` behavior from:
  - `src/backend/sdl2/rb_window.c`
  - `src/backend/sdl2/rb_init.c`
- Kept graphical test environment policy in the harness:
  - `scripts/graphical_samples.sh` still selects X11/Xvfb and dummy audio.
- Re-validated the default scripted shutdown path against `GRAPHICAL_RUNTIME=wine`;
  `Alt+F4` still behaves more like the expected guest-visible close path there.
- Kept `windowclose` out of the trusted/default shutdown path so future work
  does not treat raw X11/window-manager close as an endorsed scripted action.
- Changed `scripts/samples.sh run <graphical-sample>` to build the sample and
  then dispatch to `scripts/graphical_samples.sh`, so direct sample runs no
  longer depend on backend-only autoquit hooks.

## Boundary Rules

- Keep X11/Xvfb as the deterministic graphical test environment.
- Keep test-driver policy in scripts, not in the SDL runtime/backend path.
- If a sample needs automatic exit, drive it through one of:
  - input script `altf4`
  - posted input event from the harness
  - a sample-side timer inside the sample itself
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
- Do not promote raw X11/window-manager close into the trusted/default harness
  path without fresh Wine-reference evidence.
- Treat X11 `BadWindow` as a harness compatibility issue only if it occurs from
  the harness close action. Prefer fixing message and destroy lifecycle first.

## Files

- `scripts/graphical_samples.sh`
- `scripts/samples.sh`
- `samples/sdl2_window/*`
- `samples/sdl2_window_32/*`
- `src/backend/sdl2/rb_window.c`
- `src/backend/sdl2/rb_init.c`

## Verification

- `bash scripts/samples.sh run sdl2_window`
- `bash scripts/run_samples.sh sdl2_window`
- `bash scripts/run_samples.sh sdl2_window_32`
- `bash scripts/run_samples.sh sdl2_two_window`
- `bash scripts/run_samples.sh sdl2_two_window_32`
- `bash scripts/graphical_samples.sh inspect sdl2_window`

## Residual Runtime Issues

- The boundary cleanup above is implemented, but current `my_wine` graphical
  samples still do not all exit cleanly under Xvfb.
- As of 2026-05-16:
  - `sdl2_window` and `sdl2_two_window` still hit harness close timeouts under
    `my_wine`.
  - `sdl2_window_32` and `sdl2_two_window_32` still crash before the harness
    sees a window.
- Those failures should be treated as separate runtime/sample bugs. They are
  not a reason to restore backend-only autoquit hooks or to promote raw
  X11/window-manager close into the trusted/default harness path.
