# Window Message Dispatch

## Status

Implemented on 2026-05-16.

This landed as a shared USER32-side `WNDPROC` invocation path used by
`DispatchMessageA`, `SendMessageA`, `CallWindowProcA`, and `DestroyWindow()`.
The PE32 `DispatchMessageA()` bypass is gone, the PE32+ `WM_CLOSE` bypass is
gone, and `DestroyWindow()` now sends exactly one `WM_DESTROY` on both ABIs.
`WM_CLOSE` again flows through the guest window procedure and reaches the
default destroy path through `DefWindowProcA()` when the guest chooses it.

## Problem

`DispatchMessageA` does not consistently call the guest WNDPROC. PE32 bypasses it,
and PE32+ currently bypasses it for `WM_CLOSE`. This avoids crashes in smoke
tests but is not correct enough for Doom95, which can depend on WNDPROC behavior
for close, activation, paint, input, sizing, and internal state transitions.

## Implemented

- Added a shared `user32_call_wndproc()` helper and routed:
  - `DispatchMessageA`
  - `SendMessageA`
  - `CallWindowProcA`
  - `DestroyWindow`
  through that single callback path.
- Relied on the existing ABI-correct `WNDPROC` typedefs:
  - PE32 stdcall callback ABI
  - PE32+ Microsoft x64 callback ABI
- Restored normal `WM_CLOSE` behavior:
  - `SDL_WINDOWEVENT_CLOSE` becomes `WM_CLOSE`
  - `DispatchMessageA` calls the WNDPROC
  - if guest calls `DefWindowProcA`, default behavior destroys the window
  - `DestroyWindow` sends `WM_DESTROY`
  - guest WNDPROC can call `PostQuitMessage`
- Added a per-window `destroy_in_progress` guard so recursive destroy paths do
  not re-send `WM_DESTROY` or double-free the window entry.

## Files

- `src/msvcrt/user32_message.c`
- `src/msvcrt/user32_window.c`
- `src/msvcrt/user32_priv.h`
- `tests/test_user32_message_dispatch.c`
- `Makefile`
- `scripts/run_tests.sh`

## Verification

- `make build/test_user32_handle_ownership build/test_user32_message_dispatch build/test_sdl2_backend`
- `SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy ./build/test_user32_handle_ownership`
- `SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy ./build/test_user32_message_dispatch`
- `SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy ./build/test_sdl2_backend`

## Remaining Follow-Up

- Add a PE sample that logs full `WNDPROC` message sequences for close/input/paint parity checks across PE32 and PE32+.
- Re-run Doom95 itself once the later event-routing work is in place.
