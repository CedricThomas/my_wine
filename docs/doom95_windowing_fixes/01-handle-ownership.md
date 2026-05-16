# Handle Ownership

## Status

Implemented on 2026-05-16.

Follow-up on 2026-05-16: fixed a PE32 regression that the ownership change exposed. `WNDPROC`
now uses the Win32 callback ABI on i386, so `WM_DESTROY` callbacks do not corrupt the 32-bit
stack when `DestroyWindow()` reaches the guest window procedure. The PE32 path also no longer
calls the guest `WNDPROC` directly from `DestroyWindow()` on i386; that path already bypasses
direct guest window-procedure calls in `DispatchMessageA()`, and destruction now follows the
same rule.

## Problem

`rb_window_create()` and `CreateWindowExA()` both allocate `HANDLE_TYPE_HWIN`, but
the stored objects are different C structs. This lets code cast a backend
`rb_window` as a USER32 `wine_window_entry`, which is undefined behavior and
already affects handle-table scans such as cursor handling.

## Fix

- Add distinct handle types for backend-private objects, for example:
  - `HANDLE_TYPE_RB_WINDOW`
  - `HANDLE_TYPE_RB_SURFACE`
  - `HANDLE_TYPE_RB_PALETTE`
  - `HANDLE_TYPE_RB_CURSOR`
- Keep `HANDLE_TYPE_HWIN` exclusively for guest-visible HWND entries.
- Update backend `get_window()`, `get_surface()`, `get_palette()`, and cursor helpers to validate the backend-specific types.
- Update USER32 helpers to only accept `HANDLE_TYPE_HWIN`.
- Remove first-HWIN table scans for "active window" and expose an explicit active/focused HWND helper instead.

## Files

- `include/handle_manager.h`
- `src/backend/sdl2/rb_window.c`
- `src/backend/sdl2/rb_surface.c`
- `src/backend/sdl2/rb_palette.c`
- `src/backend/sdl2/rb_input.c`
- `src/msvcrt/user32_priv.h`
- `src/msvcrt/user32_input.c`
- `src/msvcrt/user32_window.c`
- `include/user32_types.h`

## Verification

- Added `tests/test_user32_handle_ownership.c` to assert that a USER32 `HWND` resolves to `wine_window_entry` while `entry->sdl_window` resolves only as a backend `rb_window`.
- The same test now also asserts that `DestroyWindow()` reaches the guest `WndProc` with exactly one `WM_DESTROY`, which covers the PE32 callback ABI that was missing from the first pass.
- Verified with:
  - `make build/test_user32_handle_ownership`
  - `env SDL_VIDEODRIVER=dummy ./build/test_user32_handle_ownership`
  - `make build/test_sdl2_backend`
  - `env SDL_VIDEODRIVER=dummy ./build/test_sdl2_backend --headless`
  - `make -B my_wine32`
  - `docker run --rm -v "$PWD:/project" -w /project my_wine-samples bash -lc 'bash scripts/graphical_samples.sh run-container sdl2_window_32'`
