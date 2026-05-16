# Doom95 Windowing Fix Plan

This folder breaks the current `feat/doom95-windowing` risks into executable plans.
The goal is to turn the SDL2/USER32 scaffolding into behavior that can support a
real PE such as Doom95 instead of only passing the current smoke samples.

## Order

1. [Handle Ownership](01-handle-ownership.md) - done on 2026-05-16
2. [Host Context Policy](02-host-context-policy.md) - done on 2026-05-16
3. [Window Message Dispatch](03-window-message-dispatch.md) - done on 2026-05-16
4. [Event Routing](04-event-routing.md) - done on 2026-05-16
5. [Graphical Harness Boundary](05-graphical-harness-boundary.md)
6. [Stub Hardening](06-stub-hardening.md)

## Current Status

- `01-handle-ownership.md` is implemented.
- `02-host-context-policy.md` is implemented.
- `03-window-message-dispatch.md` is implemented.
- `04-event-routing.md` is implemented.
- Backend-private SDL objects now use dedicated handle tags and validate those tags on lookup.
- USER32 active/focus tracking now uses explicit HWND state instead of scanning the handle table for the first `HANDLE_TYPE_HWIN`.
- Regression coverage now includes `test_user32_handle_ownership`.
- Guest `WNDPROC` dispatch now goes through one shared helper in `DispatchMessageA`, `SendMessageA`, `CallWindowProcA`, and `DestroyWindow()`.
- `WM_CLOSE` now reaches the guest `WNDPROC` on both PE32 and PE32+, and the default close path again drives `DestroyWindow()` -> `WM_DESTROY`.
- Regression coverage now also includes `test_user32_message_dispatch`.
- Guest-facing SDL backend calls now consistently switch to host stack and host TLS/segment context before entering SDL or glibc/libc helpers.
- `test_sdl2_backend` now includes repeated window create/resize/destroy coverage under `SDL_VIDEODRIVER=dummy`.
- SDL event routing now binds SDL/native window ids to guest `HWND`s instead of assuming one global active window target.
- `PeekMessageA(PM_NOREMOVE)` now peeks from an internal translated-message queue, and `test_user32_message_dispatch` covers two-window routing plus repeated peeks before removal.
- The current graphical sample harness still has an unresolved close-timeout problem under Xvfb; the strongest current evidence points at the harness close path boundary rather than proven new regressions in event routing.
- The PE32 `sdl2_window_32` graphical scenario was re-verified in Docker/Xvfb after fixing the exposed 32-bit callback and FS-restore regressions.

## Acceptance Criteria

- Backend SDL objects cannot be mistaken for Win32 handles.
- Guest WNDPROC dispatch works on PE32 and PE32+ without bypassing important messages.
- SDL/glibc calls consistently run with host stack and host TLS/segment state.
- SDL events route to the correct HWND, not a global active-window fallback.
- Test-only behavior stays in scripts/tests, not production backend code.
- Existing `sdl2_window`, `sdl2_window_32`, and `test_sdl2_backend` continue to pass.
