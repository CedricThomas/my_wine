# Subplan 4: DirectDraw

## Status

Implemented to the current narrowed scope, pending Doom95 validation.

The maintained tree now contains:

- `include/ddraw_types.h`
- `src/msvcrt/ddraw_priv.h`
- `src/msvcrt/ddraw_interface.c`
- `tests/test_ddraw.c`
- `samples/ddraw_sample/ddraw_sample.c`
- `samples/ddraw_sample_32/ddraw_sample_32.c`

This is no longer a greenfield plan. The remaining work is to validate the
current narrowed path against Doom95 itself.

## Goal

Provide the smallest maintained DirectDraw path that lets Doom95 create an
8-bit fullscreen-ish primary/backbuffer setup and render through
`Lock -> write -> Unlock -> Flip`.

This plan is intentionally narrower than the original one. It does not need
full DirectDraw breadth up front.

## Required End State

- `DirectDrawCreate` is exported through the loader/import tables
- Doom95 can call:
  - `SetCooperativeLevel`
  - `SetDisplayMode`
  - `CreateSurface`
  - `CreatePalette`
  - `Lock`
  - `Unlock`
  - `Flip`
  - `GetSurfaceDesc`
  - `SetPalette`
- Unsupported methods return stable errors or benign success values instead of
  crashing

## Current State

Implemented now:

- `ddraw.dll!DirectDrawCreate` is wired through `src/loader/import_table.c`
- `IDirectDraw` covers the narrowed Doom95 path:
  - `SetCooperativeLevel`
  - `SetDisplayMode`
  - `CreateSurface`
  - `CreatePalette`
  - `GetDisplayMode`
  - `GetCaps`
- `IDirectDrawSurface` covers the maintained minimal render path:
  - `GetAttachedSurface`
  - `Lock`
  - `Unlock`
  - `Blt`
  - `BltFast`
  - `Flip`
  - `GetSurfaceDesc`
  - `SetPalette`
- `IDirectDrawPalette` covers:
  - `GetEntries`
  - `SetEntries`
- Native coverage exists in `tests/test_ddraw.c`
- Real PE coverage exists in:
  - `samples/ddraw_sample/ddraw_sample.c`
  - `samples/ddraw_sample_32/ddraw_sample_32.c`

## Remaining Work

### 4.1 Stabilize the current DDraw implementation
- [x] Decide whether to keep the current single-file approach in `src/msvcrt/ddraw_interface.c`
  or split surface/palette/clipper helpers out; either is fine, but the final
  file layout should match the maintained tree, not the old `src/stubs/*` docs.
- [x] Ensure the COM layout in `include/ddraw_types.h` matches the guest ABI we
  actually use.
- [x] Review the current local implementation against `render_backend.h` and
  remove assumptions that no longer match the split SDL2 backend modules.
- [x] Finish COM ownership/teardown semantics so guest release paths are safe in
  both 32-bit and 64-bit samples without sample-side workarounds.

### 4.2 Finish the minimal surface path
- [x] Primary surface + one backbuffer flip chain
- [x] Offscreen surface creation if Doom95 uses it during setup
- [x] Palette attach/update for 8-bit rendering
- [x] `Lock`/`Unlock`/`GetSurfaceDesc` with correct pixel pointer and pitch
- [x] `Flip` backed by the existing SDL2 surface rotation/present path
- [x] `Blt`/`BltFast` only to the extent Doom95 or the maintained samples need them

### 4.3 Loader integration
- [x] Add `ddraw.dll` / `DirectDrawCreate` to `src/loader/import_table.c`
- [x] Add any needed forward declarations so the maintained build can link the symbol cleanly
- [x] Confirm 32-bit build behavior as well as the host test build

### 4.4 Verification
- [x] Add a focused native test covering `DirectDrawCreate`, display mode,
  flip-chain creation, lock/unlock, palette application, and flip
- [x] Add real sample coverage for both PE32+ and PE32:
  - `samples/ddraw_sample`
  - `samples/ddraw_sample_32`
- [ ] Run Doom95 far enough to confirm the current narrowed DDraw path matches
  the game's actual startup/render usage instead of just the focused samples.

## Scope Reduction

The original subplan aimed at a much broader DirectDraw surface/vtable matrix
than Doom95 likely needs for first boot. Defer:

- overlay support
- bitmap-based factory helpers
- broad enumeration APIs
- duplicate-surface and monitor/DC edge cases

Those can be added later if traces prove they are needed.
