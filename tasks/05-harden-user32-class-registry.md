# Harden User32 Class Registry

## Problem

The class registry is currently a fixed-size global array of 16 `WNDCLASSA`
entries. It copies the struct by value but does not take ownership of the class
name storage. That is acceptable for tiny static samples, but it is brittle for
larger user32 usage and future dialog/window work.

Affected files:

- `src/msvcrt/user32_window.c`
- `src/msvcrt/user32_priv.h`
- Possibly a new small helper module if the registry logic grows
- `tests/`

## Goal

Keep the implementation simple, but make it safe for more than toy samples:

- own class-name storage
- avoid an arbitrary tiny hard limit
- isolate class metadata from ad hoc globals

## Plan

1. Introduce an internal registry type.
   - Store entries as a small dynamically allocated array or linked list.
   - Keep linear lookup for now; hashing is unnecessary unless evidence demands it.

2. Own the class-name string.
   - Duplicate `lpszClassName` on registration.
   - Compare against owned storage in lookup.
   - Decide whether to also copy `lpszMenuName` or leave it borrowed for now.

3. Preserve duplicate-registration behavior.
   - If a class already exists, return the existing atom.
   - Keep current semantics unless a real Win32 requirement forces change.

4. Add cleanup strategy.
   - Either accept process-lifetime ownership for now and document it, or
     introduce teardown if tests need repeated global reset.

5. Expand tests.
   - Register more than 16 classes and verify success.
   - Register using non-static class-name buffers and verify lookup still works after the caller buffer changes.

## Acceptance Criteria

- Class registration no longer depends on caller-owned name storage.
- The 16-class ceiling is removed.
- Tests cover duplicate registration and copied-name behavior.

## Notes

- Keep this fix pragmatic. A simple owned dynamic array is enough; don’t overdesign a general Win32 class manager yet.
