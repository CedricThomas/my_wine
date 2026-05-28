# Cleanup Backlog

Date: 2026-05-28

This file is the active cleanup queue for the current tree. It is intentionally
short and forward-looking so the next session starts from the current frontier
instead of re-reading old completed work.

## Top Goal

Tomorrow's focus:

- Inspect `src/backend/sdl2/rb_surface.c` for the next safest narrow extraction.
- Prefer one helper/file extraction only.
- Preserve behavior and public exports.
- Keep PE32 vs PE32+ boundaries explicit.

Why this is the top goal:

- `src/msvcrt/dsound_buffer.c` has now been reduced to COM lifetime, basic
  getters, and the exported buffer vtable.
- `src/backend/sdl2/rb_surface.c` is now the next subsystem-local oversized
  file with backend-only ownership and no loader/import coupling.
- Loader/import work still remains higher-risk than another backend-local
  helper extraction.

## Current Position

What is already in a good stop state:

- `src/msvcrt/kernel32_doom95.c` is now an empty compatibility seam.
- `src/loader/pe32_entry.c` has already been split into PE32 bootstrap,
  resolve, guest-launch, and Doom95 compatibility helpers.
- USER32 window work has already been split across lifecycle, state, paint,
  focus, and class-registry helpers.
- USER32 message work is now split across:
  - `src/msvcrt/user32_message.c`
  - `src/msvcrt/user32_message_dispatch.c`
  - `src/msvcrt/user32_message_queue.c`
  - `src/msvcrt/user32_message_hook.c`
- DirectDraw has already been split across:
  - `src/msvcrt/ddraw_backend.c`
  - `src/msvcrt/ddraw_core.c`
  - `src/msvcrt/ddraw_clipper.c`
  - `src/msvcrt/ddraw_mode.c`
  - `src/msvcrt/ddraw_palette.c`
  - `src/msvcrt/ddraw_surface.c`
  - `src/msvcrt/ddraw_surface_desc.c`
  - `src/msvcrt/ddraw_surface_ops.c`
  - remaining orchestration in `src/msvcrt/ddraw_interface.c`
- This pass moved DSound buffer creation/allocation/backend setup work into
  `src/msvcrt/dsound_buffer_create.c`.
- This pass moved DirectSound buffer playback/lock/control operations into
  `src/msvcrt/dsound_buffer_control.c`.
- `src/msvcrt/dsound_buffer.c` now keeps COM lifetime, caps/basic getters,
  no-op init/restore, and the exported `IDirectSoundBufferVtbl`.
- This pass moved Doom95-specific dialog autostart and base-WAD seeding into
  `src/msvcrt/user32_dialog_doom95.c`.
- `src/msvcrt/user32_dialog.c` now keeps generic dialog class setup, modal
  state, dialog item/control bookkeeping, resource-string loading, and
  `MessageBoxA`.

## Active Queue

Priority order:

1. `src/backend/sdl2/rb_surface.c`
2. `src/backend/sdl2/rb_audio.c`
3. `src/backend/sdl2/rb_window.c`
4. `src/msvcrt/user32_dialog.c`
5. `src/loader/import_table.c`
6. `src/loader/import_resolve.c`
7. `src/msvcrt/winmm_doom95.c`

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

Run after every meaningful change:

- `make run-tests`
- `make run-samples-scenarios`
- `env SDL_AUDIODRIVER=dummy SDL_VIDEODRIVER=x11 timeout 5 ./my_wine32 ./samples/unpacked/doom95/DOOM95.EXE`

Expected Doom95 result:

- reach WAD discovery/startup
- then time out with exit status `124`
- ALSA / fluidsynth warnings are environment noise only

## Latest Verification

Latest verification on 2026-05-27 after extracting USER32 keyboard-hook state
and hook exports into `src/msvcrt/user32_message_hook.c`:

- `make run-tests` passed: 16 passed, 0 failed, 0 skipped.
- `make run-samples-scenarios` passed: console scenarios 25 passed, 0 failed,
  1 skipped; graphical scenarios 20 passed, 0 failed; unified result passed.
- Doom95 smoke reached WAD discovery/startup
  (`GetFileAttributesA('DOOM1.WAD')` and `FindFirstFileA('*.WAD')` resolving
  `DOOM1.WAD`) and then timed out with exit status `124`; ALSA/fluidsynth
  warnings remained expected environment noise.

Latest verification on 2026-05-28 after extracting DirectSound buffer creation
from `src/msvcrt/dsound_buffer.c` into `src/msvcrt/dsound_buffer_create.c`:

- What moved: `CreateSoundBuffer` allocation/default-init, primary vs secondary
  creation branching, secondary backend-buffer setup, and owner-list attach
  helpers.
- What stayed: the `IDirectSoundBufferVtbl` export plus buffer query, status,
  lock/unlock, play/stop, and control methods remain in
  `src/msvcrt/dsound_buffer.c`.
- `make run-tests` passed: 16 passed, 0 failed, 0 skipped.
- `make run-samples-scenarios` passed: console scenarios 25 passed, 0 failed,
  1 skipped; graphical scenarios 20 passed, 0 failed; unified result passed.
- Doom95 smoke reached WAD discovery/startup
  (`GetFileAttributesA('DOOM1.WAD')` and `FindFirstFileA('*.WAD')` resolving
  `DOOM1.WAD`) and then timed out with exit status `124`; ALSA/fluidsynth
  warnings remained expected environment noise.

Latest verification on 2026-05-28 after extracting Doom95-specific dialog
autostart from `src/msvcrt/user32_dialog.c` into
`src/msvcrt/user32_dialog_doom95.c`:

- What moved: Doom95 launcher autostart provider/WAD/map selection flow plus
  base-WAD state seeding.
- What stayed: generic dialog class setup, modal tracking, dialog item/control
  bookkeeping, resource-string loading, and `MessageBoxA` remain in
  `src/msvcrt/user32_dialog.c`.
- `make run-tests` passed: 16 passed, 0 failed, 0 skipped.
- `make run-samples-scenarios` passed: console scenarios 25 passed, 0 failed,
  1 skipped; graphical scenarios 20 passed, 0 failed; unified result passed.
- Doom95 smoke reached WAD discovery/startup
  (`GetFileAttributesA('DOOM1.WAD')` and `FindFirstFileA('*.WAD')` resolving
  `DOOM1.WAD`) and then timed out with exit status `124`; ALSA/fluidsynth
  warnings remained expected environment noise.

Latest verification on 2026-05-28 after extracting DirectSound buffer
playback/lock/control operations from `src/msvcrt/dsound_buffer.c` into
`src/msvcrt/dsound_buffer_control.c`:

- What moved: `Lock`/`Unlock`, `Play`/`Stop`, status/current-position, and
  format/volume/pan/frequency control methods plus their backend-facing helper
  logic.
- What stayed: COM lifetime, caps/basic getters, no-op init/restore, and the
  exported `IDirectSoundBufferVtbl` remain in `src/msvcrt/dsound_buffer.c`.
- `make run-tests` passed: 16 passed, 0 failed, 0 skipped.
- `make run-samples-scenarios` passed: console scenarios 25 passed, 0 failed,
  1 skipped; graphical scenarios 20 passed, 0 failed; unified result passed.
- Doom95 smoke reached WAD discovery/startup
  (`GetFileAttributesA('DOOM1.WAD')` and `FindFirstFileA('*.WAD')` resolving
  `DOOM1.WAD`) and then timed out with exit status `124`; ALSA/fluidsynth
  warnings remained expected environment noise.

## Session Rules

For the next cleanup pass:

1. Read this file first.
2. Start with the top goal unless a narrower lower-risk seam is clearly better.
3. Make one extraction only.
4. Verify fully.
5. Update this file with what moved, what stayed, and the exact command results.
