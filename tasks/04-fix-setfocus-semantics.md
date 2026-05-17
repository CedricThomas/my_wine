# Fix SetFocus Semantics

## Problem

`SetFocus()` currently returns the new focus window instead of the previous one.
The current unit test also locks in that incorrect behavior, which makes future
Win32-compat fixes harder.

Affected files:

- `src/msvcrt/user32_window.c`
- `src/msvcrt/user32_priv.h`
- `tests/test_user32_handle_ownership.c`
- Possibly new focus-specific test coverage

## Goal

Align `SetFocus()` more closely with Win32 semantics while preserving the
branch’s current active/focus bookkeeping model.

## Plan

1. Capture previous focus before mutating state.
   - Compute `prev = user32_get_focus_window()`.
   - Return `prev` after updating the new focus target.

2. Clarify active-vs-focus policy.
   - Decide whether every successful `SetFocus()` should also update active window.
   - Keep the current simplified model if needed, but document it explicitly.

3. Update tests.
   - Fix the existing ownership test to expect previous focus.
   - Add a multi-window case:
     - create two windows
     - focus A, then focus B
     - assert `SetFocus(B)` returns A

4. Consider message implications.
   - If later phases need `WM_SETFOCUS` / `WM_KILLFOCUS`, note that this change should be the base for that work.

## Acceptance Criteria

- `SetFocus()` returns the previous focus window.
- Tests cover at least one focus transition between different windows.
- The active/focus bookkeeping remains internally consistent.
