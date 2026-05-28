# Cleanup Backlog

Date: 2026-05-28

This file is the active cleanup queue for the current tree. It is intentionally
short, forward-looking, and planning-only so the next session starts from the
current frontier instead of re-reading old completed work.

It should not duplicate per-pass verification results or mini change logs.
Those belong in command output and git history.

## Top Goal

Tomorrow's focus:

- Inspect `src/backend/sdl2/rb_audio.c` for the next safest narrow extraction.
- Prefer one helper/file extraction only.
- Preserve behavior and public exports.
- Keep PE32 vs PE32+ boundaries explicit.

Why this is the top goal:

- `src/backend/sdl2/rb_surface.c` has now been reduced to surface creation,
  ownership, lock/unlock, and blit orchestration.
- `src/backend/sdl2/rb_audio.c` is now the next oversized backend-local file
  with no loader/import coupling.
- Loader/import work still remains higher-risk than another backend-local
  helper extraction.

## Current Position

Stable enough to leave alone unless new evidence appears:

- `src/msvcrt/kernel32_doom95.c` remains an empty compatibility seam.
- `src/loader/pe32_entry.c` has already been split into PE32 bootstrap,
  resolve, guest-launch, and Doom95 compatibility helpers.
- USER32 window and message code is already separated into lifecycle/state,
  paint/focus/class-registry, dispatch, queue, and hook files.
- DirectDraw is already split across backend/core/clipper/mode/palette/surface
  helper files, with orchestration still centered in
  `src/msvcrt/ddraw_interface.c`.
- DirectSound buffer creation and buffer control paths already live outside
  `src/msvcrt/dsound_buffer.c`.
- Doom95 dialog autostart behavior already lives in
  `src/msvcrt/user32_dialog_doom95.c`.
- SDL surface presentation now lives in
  `src/backend/sdl2/rb_surface_present.c`.

## Active Queue

Priority order:

1. `src/backend/sdl2/rb_audio.c`
2. `src/backend/sdl2/rb_window.c`
3. `src/msvcrt/user32_dialog.c`
4. `src/loader/import_table.c`
5. `src/loader/import_resolve.c`
6. `src/msvcrt/winmm_doom95.c`

Selection bias:

- Prefer the least-coupled oversized file.
- Prefer helper extraction over logic rewrite.
- Prefer subsystem-local ownership cleanup over cross-cutting redesign.
- Treat loader/import work as higher-risk than USER32/DDraw/DSound/backend
  helper extraction.

## Not Next

Do not spend the next pass here unless a very narrow seam becomes obvious:

- `src/msvcrt/ddraw_interface.c`
- `src/msvcrt/kernel32_doom95.c`
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
