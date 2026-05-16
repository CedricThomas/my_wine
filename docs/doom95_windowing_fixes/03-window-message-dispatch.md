# Window Message Dispatch

## Problem

`DispatchMessageA` does not consistently call the guest WNDPROC. PE32 bypasses it,
and PE32+ currently bypasses it for `WM_CLOSE`. This avoids crashes in smoke
tests but is not correct enough for Doom95, which can depend on WNDPROC behavior
for close, activation, paint, input, sizing, and internal state transitions.

## Fix

- Implement a single guest-callback helper for WNDPROC invocation.
- Make the helper handle:
  - PE32 stdcall callback ABI
  - PE32+ Microsoft x64 callback ABI
  - guest stack and host stack transitions
  - preserving/restoring FS/GS state
- Route `DispatchMessageA`, `SendMessageA`, `CallWindowProcA`, and `DestroyWindow` through that helper.
- Restore normal `WM_CLOSE` behavior:
  - `SDL_WINDOWEVENT_CLOSE` becomes `WM_CLOSE`
  - `DispatchMessageA` calls the WNDPROC
  - if guest calls `DefWindowProcA`, default behavior destroys the window
  - `DestroyWindow` sends `WM_DESTROY`
  - guest WNDPROC can call `PostQuitMessage`
- Do not special-case messages because callback invocation is unstable. Fix callback invocation instead.

## Files

- `src/msvcrt/user32_message.c`
- `src/msvcrt/user32_window.c`
- `src/msvcrt/user32_priv.h`
- `src/loader/pe32_entry.c`
- `src/loader/pe32_run_guest.S`
- `src/run_guest.S`

## Verification

- Add a sample that records WNDPROC messages into stdout:
  - create
  - close
  - destroy
  - key down/up
  - mouse button
- Verify PE32 and PE32+ produce the same message sequence.
- Run Doom95 far enough to create its main window and process at least one message loop iteration.
