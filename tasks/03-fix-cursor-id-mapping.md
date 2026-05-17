# Fix Cursor ID Mapping

## Problem

`LoadCursorA()` forwards raw Win32 IDC values like `32512` and `32515`, but
`rb_cursor_create()` currently expects a private small integer range. Most
system cursors therefore collapse to the default arrow cursor.

Affected files:

- `src/msvcrt/user32_input.c`
- `src/backend/sdl2/rb_input.c`
- `include/user32_types.h`
- `tests/` or graphical samples

## Goal

Support stable Win32-to-SDL system cursor mapping without leaking backend-local
cursor IDs into the user32 layer.

## Plan

1. Pick one translation boundary.
   - Preferred: let `rb_cursor_create()` accept real Win32 IDC values.
   - Alternative: translate IDC values in `LoadCursorA()` and keep the backend-private enum.

2. Normalize the supported cursor set.
   - At minimum: `IDC_ARROW`, `IDC_CROSS`, `IDC_IBEAM`, `IDC_WAIT`, `IDC_HAND`.
   - For unsupported values, fall back to arrow rather than failing.

3. Update comments and API contract.
   - `render_backend.h` currently documents `IDC_ARROW=0, IDC_CROSS=1`, which no longer matches the user32 call site.
   - Rewrite the comment so future callers know whether the API takes Win32 IDC constants or backend-local IDs.

4. Add tests.
   - Native unit test for `LoadCursorA()` returning non-null handles for several standard IDC values.
   - Optional backend test that exercises `rb_cursor_create()` directly.

## Acceptance Criteria

- Standard Win32 cursors resolve consistently.
- The render-backend contract documents the expected input clearly.
- No private ID scheme remains implicit between user32 and SDL2 backend layers.
