# Host Context Policy

## Problem

SDL/glibc calls are inconsistent. Some use `rb_call_on_host_stack()`, while many
only restore GS/FS and continue running on the guest stack. That is fragile for
PE32+ and real games because host libraries expect host stack, host TLS, and
normal ABI alignment.

## Fix

- Define a rule: every call into SDL, X11, glibc allocation/free, or libc API from guest-facing paths must go through a host-context wrapper.
- Keep pure handle-table operations and simple struct assignments outside the wrapper.
- Add small wrapper functions for currently direct calls:
  - window show/hide, move, resize, title, fullscreen, cursor, mouse warp
  - surface create/free/blit/stretch/fill/palette/window-surface update
  - timer, keyboard, joystick, audio calls
- Make `rb_call_on_host_stack()` preserve alignment explicitly on x86_64. The current path uses `unix_stack_ptr_val` directly; document or enforce alignment.
- Add debug assertions where possible to catch accidental host calls while guest GS/FS or guest stack is active.

## Files

- `src/backend/sdl2/rb_sdl2_priv.h`
- `src/backend/sdl2/rb_window.c`
- `src/backend/sdl2/rb_surface.c`
- `src/backend/sdl2/rb_input.c`
- `src/backend/sdl2/rb_audio.c`
- `src/backend/sdl2/rb_palette.c`
- `src/backend/sdl2/rb_event.c`

## Verification

- Run existing SDL backend tests with `SDL_VIDEODRIVER=dummy`.
- Run graphical samples under Docker/Xvfb.
- Add a regression sample that creates, resizes, destroys, and recreates a window repeatedly.
