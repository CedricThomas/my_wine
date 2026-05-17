# Fix BeginPaint/EndPaint DC Lifecycle

## Problem

`BeginPaint()` allocates and returns an HDC handle, but it does not store that
handle into `PAINTSTRUCT.hdc`. `EndPaint()` then reads `lpPaint->hdc`, which
makes the release path inconsistent and potentially leaks or mis-releases the DC.

Affected files:

- `src/msvcrt/user32_window.c`
- `include/user32_types.h`
- `tests/` (new targeted test)

## Goal

Make `BeginPaint()`/`EndPaint()` internally consistent and close to Win32
semantics for the limited mock implementation.

## Plan

1. Fix `BeginPaint()`.
   - Populate `lpPaint->hdc` with the allocated HDC handle before returning.
   - Ensure all `PAINTSTRUCT` fields are initialized deterministically.

2. Tighten `EndPaint()`.
   - Validate that `lpPaint->hdc` is either zero or a `HANDLE_TYPE_DC`.
   - Release the backend DC only when the handle is valid.
   - Free the HDC handle exactly once.

3. Decide whether `BeginPaint()` should delegate through `GetDC()`.
   - If yes, collapse duplicated logic.
   - If no, keep the implementation separate but make the shared assumptions explicit.

4. Add a native test.
   - Register/create a window.
   - Call `BeginPaint()`.
   - Assert returned HDC matches `PAINTSTRUCT.hdc`.
   - Call `EndPaint()` and confirm the DC handle is no longer valid.

## Acceptance Criteria

- `PAINTSTRUCT.hdc` is always populated on successful `BeginPaint()`.
- `EndPaint()` can release the DC using only the supplied `PAINTSTRUCT`.
- A regression test covers the full path.
