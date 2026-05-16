# Event Routing

## Problem

SDL events are assigned to a single global active window. This is not enough for
dialogs, hidden helper windows, recreated windows, or stale SDL events after a
destroy. The current X11 `BadWindow` fallback also records only a global pending
close, so it can close the wrong HWND.

## Fix

- Store the SDL window id in backend window state.
- Maintain a mapping from SDL window id to guest HWND.
- Update the mapping when:
  - a window is created
  - a window is destroyed
  - focus changes
- Translate SDL window events using the SDL window id instead of `g_active_window`.
- Keep `g_active_window` only as a fallback for keyboard events that arrive without a window id.
- If an X11 `BadWindow` fallback is still needed for Xvfb/xdotool, bind it to the native/SDL window that triggered it, not a global bit.
- Implement `PM_NOREMOVE` for `PeekMessageA` using an internal translated-message queue. SDL peeking alone is not enough because some SDL events are consumed while translating.

## Files

- `src/backend/sdl2/rb_event.c`
- `src/backend/sdl2/rb_window.c`
- `src/backend/sdl2/rb_sdl2_priv.h`
- `src/msvcrt/user32_message.c`
- `src/msvcrt/user32_window.c`

## Verification

- Add a two-window sample and close the second window first.
- Verify messages target the correct HWND.
- Add a `PeekMessageA(PM_NOREMOVE)` regression that peeks the same message twice before removal.
