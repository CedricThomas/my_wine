# Fix Flip-Chain Semantics

## Problem

The SDL2 backend flip-chain path does not currently match the DirectDraw model.
`rb_surface_flip()` presents the caller surface first and only then swaps pixel
buffers with the window-owned backbuffer. For a primary+backbuffer flow, this
shows the stale front buffer on the first flip.

Affected files:

- `src/backend/sdl2/rb_surface.c`
- `src/backend/sdl2/rb_sdl2_priv.h`
- `tests/test_sdl2_backend.c`

## Goal

Make the backend support the intended DirectDraw-style flow:

1. Lock/write/unlock the backbuffer
2. Call `Flip()` on the primary surface
3. Present the just-rendered backbuffer contents
4. Rotate front/back buffers for the next frame

## Plan

1. Define flip-chain ownership explicitly.
   - Document which surface handle is the primary/front buffer.
   - Document which handle represents the backbuffer.
   - Decide whether the window owns the chain metadata or whether surfaces link to each other.

2. Refactor the internal surface state.
   - Add enough metadata to distinguish primary vs. backbuffer reliably.
   - Avoid relying on “surface passed to `rb_surface_flip()`” as the source of truth for presentation.

3. Fix `rb_surface_flip()`.
   - When flipping a primary surface with an attached backbuffer, present the backbuffer contents to the window.
   - After present, rotate the buffers so the old front becomes the next backbuffer.
   - Preserve palette association and pitch invariants across the swap.

4. Validate destruction rules.
   - Ensure destroying the window or primary does not double-free the backbuffer.
   - Ensure replacing a flip chain releases prior chain state cleanly.

5. Add targeted tests.
   - Create a backend test that builds a flip chain, writes a sentinel pattern into the backbuffer, flips, then confirms buffer rotation happened as expected.
   - Add a regression that exercises at least two consecutive flips.

## Acceptance Criteria

- The first `rb_surface_flip(primary)` presents freshly rendered backbuffer pixels.
- Two consecutive flips preserve deterministic front/back rotation.
- No double-free or leaked handle behavior when the window or chain is destroyed.
- `tests/test_sdl2_backend.c` covers the flip-chain path.

## Notes

- This is the highest-priority quality fix because subplan 04 DirectDraw depends on exact flip semantics.
