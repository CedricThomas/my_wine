# Handle Ownership

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

## Verification

- Add a unit check that creates a USER32 window and asserts the HWND resolves to `wine_window_entry`, while its `sdl_window` resolves only as a backend window.
- Run `make all`.
- Run `bash scripts/run_samples.sh sdl2_window`.
- Run `bash scripts/run_samples.sh sdl2_window_32`.
