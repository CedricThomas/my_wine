# Event Routing

## Status

Implemented on 2026-05-16.

This replaced the global SDL event target assumption with per-window routing.
Backend window state now records SDL/native window ids, USER32 binds each guest
`HWND` to that backend window, SDL events resolve through that binding, and the
X11 `BadWindow` fallback now closes only the guest window that owned the native
X11 resource. `PeekMessageA(PM_NOREMOVE)` also now uses an internal translated
message queue, so peeking no longer consumes the translated message.

## Problem

SDL events are assigned to a single global active window. This is not enough for
dialogs, hidden helper windows, recreated windows, or stale SDL events after a
destroy. The current X11 `BadWindow` fallback also records only a global pending
close, so it can close the wrong HWND.

## Implemented

- Store the SDL window id in backend window state.
- Store the native/X11 window id in backend window state when SDL exposes one.
- Maintain a backend route table from SDL window id and native window id to guest
  `HWND`.
- Bind and refresh that route when:
  - a USER32 window is created
  - a USER32 window is destroyed
  - an SDL-backed window is recreated during fullscreen transitions
- Translate SDL mouse/window/text/keyboard events using the SDL window id first.
- Keep `g_active_window` only as a fallback for keyboard/text events that arrive
  without a window id.
- Update the active fallback when SDL focus changes.
- Route the X11 `BadWindow` fallback through the native window id that triggered
  the error instead of a process-global close bit.
- Implement `PeekMessageA(PM_NOREMOVE)` using a translated-message queue so the
  same translated message can be peeked multiple times before removal.
- Preserve `HWND` targeting when USER32 posts synthetic input/window messages by
  pushing SDL events with the bound SDL window id.

## Files

- `src/backend/sdl2/rb_event.c`
- `src/backend/sdl2/rb_window.c`
- `src/backend/sdl2/rb_sdl2_priv.h`
- `src/backend/sdl2/rb_init.c`
- `src/msvcrt/user32_message.c`
- `src/msvcrt/user32_window.c`
- `tests/test_user32_message_dispatch.c`

## Verification

- `make build/test_user32_handle_ownership build/test_user32_message_dispatch build/test_sdl2_backend`
- `SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy ./build/test_user32_handle_ownership`
- `SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy ./build/test_user32_message_dispatch`
- `SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy ./build/test_sdl2_backend`

`test_user32_message_dispatch` now covers:

- posting a message to the second of two windows and verifying the translated
  `MSG.hwnd` still points at that second window
- peeking the same translated message twice with `PM_NOREMOVE`
- removing that same message with `PM_REMOVE`
- closing the second window first without affecting the first window's earlier
  destroy path

## Remaining Follow-Up

- Add a graphical two-window sample that drives real SDL close/focus events
  through the same routing path outside the dummy driver.
