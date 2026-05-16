# Stub Hardening

## Problem

Several USER32 functions return sentinels or unconditional success. That is fine
as scaffolding, but some are likely to matter for Doom95 once more of the game
initialization runs.

## Fix

Prioritize these stubs:

- Focus and active window:
  - `GetActiveWindow`
  - `SetActiveWindow` if imported later
  - `GetFocus`
  - `SetFocus`
- Paint and invalidation:
  - `InvalidateRect`
  - `ValidateRect`
  - `UpdateWindow`
  - `BeginPaint`
  - `EndPaint`
- Window geometry:
  - `AdjustWindowRect`
  - `AdjustWindowRectEx`
  - `GetSystemMetrics`
- Cursor:
  - `ClipCursor`
  - `SetCursor`
  - previous-cursor return tracking
- Icon/class metadata:
  - `LoadIconA`
  - class icon/cursor fields in `WNDCLASSA`

## Implementation Notes

- Do not implement everything at once.
- Convert each stub only when a Doom95 trace or sample proves it matters.
- For each converted stub, add a targeted sample or native test.
- Keep a table of unsupported behavior in docs so future failures are easy to triage.

## Verification

- Add trace logging for unsupported USER32 paths at debug level 1 or 2.
- Run Doom95 startup and record the first missing/stubbed call that changes behavior.
