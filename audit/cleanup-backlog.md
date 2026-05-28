# Cleanup Backlog

Date: 2026-05-28

This file is intentionally thin. Keep only the active frontier, current
ownership facts, and the next best target.

## Top Goal

Next pass:

- Move to `src/msvcrt/ddraw_interface.c`.
- Prefer one helper/file extraction only.
- Preserve behavior and public exports.
- Keep PE32 vs PE32+ boundaries explicit.

Why this is next:

- `ddraw_interface.c` is now the next queued mixed-responsibility graphics file
  after the SDL2 window host-call split.
- The SDL2 backend window ownership is clearer now that host-stack SDL window
  calls live in `src/backend/sdl2/rb_window_host.c`.
- Loader/import work still remains higher risk than a graphics-local
  extraction.

## Current Position

Leave these alone unless a narrower seam appears:

- `src/backend/sdl2/rb_surface_present.c` owns SDL surface presentation.
- `src/backend/sdl2/rb_window_host.c` owns SDL window host-stack call shims.
- `src/backend/sdl2/rb_window_state.c` owns backend-local window state helpers.
- `src/msvcrt/winmm_doom95.c` now keeps guest-facing stream state and exports.
- `src/msvcrt/winmm_doom95_midi_backend.c` owns FluidSynth backend helpers.
- Loader/import and PE32 Doom95 helper splits are stable enough for now.

## Active Queue

Priority order:

1. `src/msvcrt/ddraw_interface.c`
2. `src/backend/sdl2/rb_event.c`

Selection bias:

- Prefer the least-coupled oversized file.
- Prefer helper extraction over logic rewrite.
- Prefer subsystem-local ownership cleanup over cross-cutting redesign.
- Treat loader/import work as higher-risk than USER32/DDraw/DSound/backend
  helper extraction.

## Not Next

Do not spend the next pass here unless a very narrow seam becomes obvious:

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
2. Start with the top goal unless a narrower lower-risk seam is clearly better.
3. Make one extraction only.
4. Use targeted compile/test checks during editing.
5. Run the full verification loop once before stopping.
6. Update this file only for queue/ownership/priority changes, not per-pass
   verification logs.
