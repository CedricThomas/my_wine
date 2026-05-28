# Cleanup Backlog

Date: 2026-05-28

This file is intentionally thin. Keep only the active frontier, current
ownership facts, and the next best target.

## Top Goal

Next pass:

- Move to `src/backend/sdl2/rb_init.c`.
- Prefer one architecture-significant seam only.
- Preserve behavior and public exports.
- Keep PE32 vs PE32+ boundaries explicit.

Why this is next:

- SDL init/shutdown still owns host-library policy, fallback sequencing, and
  process-global backend state in one place.
- `rb_init.c` is now the clearest live seam for reducing backend-global state
  without entering the higher-risk loader/import core.
- Path runtime virtualization moved ahead enough that backend init/state is the
  next higher-payoff ownership cleanup.

## Current Position

Leave these alone unless a narrower seam appears:

- `src/backend/sdl2/rb_surface_present.c` owns SDL surface presentation.
- `src/backend/sdl2/rb_window_host.c` owns SDL window host-stack call shims.
- `src/backend/sdl2/rb_window_state.c` owns backend-local window state helpers.
- `src/backend/sdl2/rb_runtime_state.c` owns backend-global init/shutdown,
  signal-handler, and cross-thread shutdown/X11 runtime state.
- `src/backend/sdl2/rb_driver_policy.c` owns requested/active SDL driver
  capture plus video/audio fallback policy.
- `src/backend/sdl2/rb_event.c` owns SDL event translation only.
- `src/backend/sdl2/rb_event_queue.c` owns SDL host queue polling, bad-window
  delivery, and wait/peek dispatch.
- `src/msvcrt/handle_manager_runtime.c` owns the process-global handle table
  state and std-handle seeding behind the handle-manager API.
- `src/msvcrt/ddraw_surface_create.c` owns DirectDraw surface-create
  orchestration after guest descriptor parsing.
- `src/msvcrt/kernel32_path.c` now owns guest current-directory runtime state
  and path normalization without mutating the host process cwd.
- `src/msvcrt/kernel32_doom95.c` now owns Doom95 launcher base-WAD path
  shaping and guest runtime-slot seeding; `user32` only drives dialog controls.
- `src/msvcrt/winmm_doom95.c` now keeps guest-facing stream state and exports.
- `src/msvcrt/winmm_doom95_midi_backend.c` owns FluidSynth backend helpers.
- Loader/import and PE32 Doom95 helper splits are stable enough for now.

## Active Queue

Priority order:

1. `src/backend/sdl2/rb_init.c`
2. `include/handle_manager.h` + `src/msvcrt/handle_manager.c`
3. `src/msvcrt/dsound_interface.c`
4. loader/import state seams adjacent to `loader_state.h`

Selection bias:

- Prefer reducing global state over shrinking file length.
- Prefer host/guest boundary cleanup over subsystem-local polish.
- Prefer contract/state extraction over helper-only splits.
- Treat loader/import core as high payoff but higher risk; approach it only
  after cleaner state seams exist around path/backend/runtime setup.

## Not Next

Do not spend the next pass here unless the active architecture-first queue is blocked:

- `src/msvcrt/ddraw_interface.c`
- `src/msvcrt/kernel32_doom95.c`
- loader/import files
- the already-split PE32 bootstrap cluster

## Verification Baseline

Run once per extraction before stopping:

- `make run-tests`
- `make run-samples-scenarios`
- `env SDL_AUDIODRIVER=dummy SDL_VIDEODRIVER=x11 timeout 5 ./my_wine32 ./samples/unpacked/doom95/DOOM95.EXE`

Expected Doom95 result:

- reach WAD discovery/startup
- then time out with exit status `124`
- ALSA / fluidsynth warnings are environment noise only

## Session Rules

For the next cleanup pass:

1. Read this file first.
2. Start with the top goal unless a higher-payoff architectural seam is clearly better.
3. Make one architectural move only.
4. Use targeted compile/test checks during editing.
5. Run the full verification loop once before stopping.
6. Update this file only for queue/ownership/priority changes, not per-pass
   verification logs.
