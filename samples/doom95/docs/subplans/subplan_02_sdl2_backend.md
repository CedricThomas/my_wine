# Subplan 2: SDL2 Backend

## Status

Completed in the current tree.

## What Exists Now

- `include/render_backend.h`
- `src/backend/sdl2/rb_init.c`
- `src/backend/sdl2/rb_window.c`
- `src/backend/sdl2/rb_surface.c`
- `src/backend/sdl2/rb_palette.c`
- `src/backend/sdl2/rb_audio.c`
- `src/backend/sdl2/rb_event.c`
- `src/backend/sdl2/rb_input.c`
- `src/backend/sdl2/rb_sdl2_priv.h`
- `tests/test_sdl2_backend.c`
- `Makefile` wiring for backend objects and SDL2-backed tests

Implemented capabilities:

- Window lifecycle, show/hide, resize, title, fullscreen helpers
- Surface creation, lock/unlock, blit, flip-chain rotation, palette application
- Palette create/set/get plumbing
- Audio backend primitives used by future DirectSound work
- SDL event translation into `rb_msg_t`
- Timer, joystick, keyboard, cursor, and display helpers

## Verification

Primary coverage is `tests/test_sdl2_backend.c`, which exercises:

- backend init/shutdown
- window creation and recreation
- DC and cursor helpers
- palette round-tripping
- surface lock/unlock and descriptor reporting
- flip-chain behavior
- event queue behavior
- audio buffer basics

## Scope Change

This subplan no longer owns any remaining Doom95 work. Future rendering or
audio issues should be addressed in the higher-level DDraw/DSound plans, not by
expanding the backend plan again unless a concrete backend defect is found.
