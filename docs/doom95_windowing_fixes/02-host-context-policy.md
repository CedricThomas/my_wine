# Host Context Policy

Status: implemented on 2026-05-16

## Problem

SDL/glibc calls are inconsistent. Some use `rb_call_on_host_stack()`, while many
only restore GS/FS and continue running on the guest stack. That is fragile for
PE32+ and real games because host libraries expect host stack, host TLS, and
normal ABI alignment.

One PE32 bug from the handle-ownership work is now fixed here already: on i386,
`rb_host_context_leave()` must not try to restore guest FS when no host FS selector
was captured. If `loader_get_host_fs_selector()` returns `0`, enter/leave must both
act as no-ops for FS switching.

## Implemented

- Defined and applied the rule that guest-facing SDL backend paths must reach SDL/glibc/libc through host-context wrappers.
- Kept handle-table operations, state bookkeeping, and simple struct assignment on the guest side.
- Hardened `rb_call_on_host_stack()`:
  - x86_64 now masks `unix_stack_ptr_val` to a 16-byte-aligned host stack before the call.
  - debug assertions now check expected host-context state before switching stacks.
  - i386 keeps the required no-op behavior when `loader_get_host_fs_selector()` returns `0`.
- Added shared host wrappers in `rb_sdl2_priv.h` for `malloc`, `calloc`, `free`, and `getenv`.
- Reworked direct SDL/libc call sites in guest-facing paths to use host-stack wrappers:
  - window show/hide, move, resize, title, fullscreen rebuild, cursor, mouse warp, and window-surface fetch
  - surface create/free/blit/stretch/fill/palette/window-surface update
  - timer, keyboard, joystick, cursor creation/show, and audio open/close
  - palette allocation and `SDL_SetPaletteColors`
  - exposed-window repaint path in event handling

## Notes

- This pass covered the guest-facing SDL backend files listed below.
- It did not add a new standalone regression sample; instead, the repeated window create/resize/destroy/recreate flow was added to `test_sdl2_backend`.
- `rb_init.c` already used host-stack wrappers for its SDL init/shutdown/display-mode entry points and was not materially changed in this pass.

## Files

- `src/backend/sdl2/rb_sdl2_priv.h`
- `src/backend/sdl2/rb_window.c`
- `src/backend/sdl2/rb_surface.c`
- `src/backend/sdl2/rb_input.c`
- `src/backend/sdl2/rb_audio.c`
- `src/backend/sdl2/rb_palette.c`
- `src/backend/sdl2/rb_event.c`
- `tests/test_sdl2_backend.c`

## Verification

- `make build/test_sdl2_backend`
- `SDL_VIDEODRIVER=dummy ./build/test_sdl2_backend`
- `test_sdl2_backend` now exercises:
  - window move/resize/title/show/hide/fullscreen(windowed rebuild)/warp
  - repeated create -> resize -> destroy window flow

## Remaining Follow-Up

- Re-run graphical samples under Docker/Xvfb as part of the broader Doom95 windowing series.
- If we want a sample-level regression separate from the unit/backend test binary, add a dedicated graphical recreate-window sample later.
