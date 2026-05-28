# Cleanup Backlog

Date: 2026-05-28

This file is the active cleanup queue for the current tree. It is intentionally
short, forward-looking, and planning-only so the next session starts from the
current frontier instead of re-reading old completed work.

It should not duplicate per-pass verification results or mini change logs.
Those belong in command output and git history.

## Top Goal

Tomorrow's focus:

- Move to `src/loader/import_table.c` unless a smaller lower-risk seam appears
  nearby.
- Prefer one helper/file extraction only.
- Preserve behavior and public exports.
- Keep PE32 vs PE32+ boundaries explicit.

Why this is the top goal:

- `src/backend/sdl2/rb_window.c` now keeps the guest-facing window entrypoints,
  while backend-local window identity, cursor, surface-detach, and guest-rebind
  helpers live in `src/backend/sdl2/rb_window_state.c`.
- The previous `src/msvcrt/user32_dialog.c` umbrella file is gone: dialog
  creation now lives in `src/msvcrt/user32_dialog_create.c`, modal completion
  in `src/msvcrt/user32_dialog_lifecycle.c`, dialog control-message handling in
  `src/msvcrt/user32_dialog_controls.c`, dialog item-state ownership in
  `src/msvcrt/user32_dialog_state.c`, dialog modal/class ownership in
  `src/msvcrt/user32_dialog_modal.c`, dialog message routing in
  `src/msvcrt/user32_dialog_message.c`, resource-backed
  `LoadStringA`/`MessageBoxA` utilities in
  `src/msvcrt/user32_dialog_resources.c`, dialog item lookup/button/text
  exports in `src/msvcrt/user32_dialog_items.c`, and Doom95 autostart policy in
  `src/msvcrt/user32_dialog_doom95.c`.
- Loader/import work is now the next intended frontier despite the higher risk.

## Current Position

Stable enough to leave alone unless new evidence appears:

- `src/msvcrt/kernel32_doom95.c` remains an empty compatibility seam.
- `src/loader/pe32_entry.c` has already been split into PE32 bootstrap,
  resolve, guest-launch, and Doom95 compatibility helpers.
- Doom95 guest argument shaping now lives in
  `src/loader/pe32_doom95_command.c`, leaving
  `src/loader/pe32_doom95_compat.c` focused on runtime slot seeding plus
  command-helper orchestration.
- USER32 window and message code is already separated into lifecycle/state,
  paint/focus/class-registry, dispatch, queue, and hook files.
- DirectDraw is already split across backend/core/clipper/mode/palette/surface
  helper files, with orchestration still centered in
  `src/msvcrt/ddraw_interface.c`.
- DirectSound buffer creation and buffer control paths already live outside
  `src/msvcrt/dsound_buffer.c`.
- Doom95 dialog autostart behavior already lives in
  `src/msvcrt/user32_dialog_doom95.c`.
- USER32 dialog creation now lives in
  `src/msvcrt/user32_dialog_create.c`.
- USER32 dialog modal completion/teardown now lives in
  `src/msvcrt/user32_dialog_lifecycle.c`.
- USER32 dialog modal/class ownership now lives in
  `src/msvcrt/user32_dialog_modal.c`.
- USER32 dialog message routing now lives in
  `src/msvcrt/user32_dialog_message.c`.
- USER32 dialog resource/UI utility exports now live in
  `src/msvcrt/user32_dialog_resources.c`.
- USER32 dialog item lookup/button/text exports now live in
  `src/msvcrt/user32_dialog_items.c`.
- SDL surface presentation now lives in
  `src/backend/sdl2/rb_surface_present.c`.
- SDL backend-local window state ownership now lives in
  `src/backend/sdl2/rb_window_state.c`.

## Active Queue

Priority order:

1. `src/loader/import_table.c`
2. `src/loader/import_resolve.c`
3. `src/msvcrt/winmm_doom95.c`
4. `src/backend/sdl2/rb_window.c`

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
