# DOOM95 Implementation Tracking

> Status: re-audited work plan against the current tree.
>
> This index was refreshed after the PE32/runtime cleanup. It tracks the
> remaining Doom95-specific work using the files and tests that exist now.
> Some lower-level reference docs under `reference/` and `by_dll/` are still
> historical and may overstate missing work.

Current picture:

- Subplan 1 is complete.
- Subplan 2 is complete and backed by `tests/test_sdl2_backend.c`.
- Subplan 3 is complete and backed by `tests/test_user32_handle_ownership.c`
  and `tests/test_user32_message_dispatch.c`.
- Subplan 4 is now wired into the maintained build/import/test path via
  `include/ddraw_types.h`, `src/msvcrt/ddraw_priv.h`,
  `src/msvcrt/ddraw_interface.c`, `tests/test_ddraw.c`,
  `samples/ddraw_sample`, and `samples/ddraw_sample_32`.
- The remaining DDraw work is now real-game validation against Doom95 rather
  than local render-path or teardown cleanup.
- Parsing `samples/unpacked/doom95/DOOM95.EXE` confirms imports from
  `KERNEL32`, `ADVAPI32`, `WINMM`, `GDI32`, `USER32`, `DPLAY`, `DDRAW`,
  and `DSOUND`, so the remaining work is centered on those exact DLLs rather
  than speculative extras.
- Subplans 5-7 remain open, but their scope is smaller than the original plan
  because the SDL2 backend and core `user32` work already exist.

| # | Subplan | Current State | Status |
|---|---------|---------------|--------|
| 1 | [Foundation](subplans/subplan_01_foundation.md) | PE32 loader/runtime baseline complete | ✅ Done |
| 2 | [SDL2 Backend](subplans/subplan_02_sdl2_backend.md) | Backend implemented and tested | ✅ Done |
| 3 | [Windowing](subplans/subplan_03_windowing.md) | `user32` window/message/input path implemented and tested | ✅ Done |
| 4 | [DirectDraw](subplans/subplan_04_ddraw.md) | Minimal DDraw path is implemented and tested; remaining work is Doom95 validation | ◐ In progress |
| 5 | [DirectSound](subplans/subplan_05_dsound.md) | Minimal DSound COM shim still needed on top of existing backend audio | ☐ Open |
| 6 | [System APIs](subplans/subplan_06_system_apis.md) | Import audit complete; implement the confirmed kernel32/winmm/advapi32/dplay gaps | ☐ Open |
| 7 | [Final Integration](subplans/subplan_07_integration.md) | Run Doom95, then implement the confirmed dialog/GDI/runtime gaps | ☐ Open |

**Recommended execution order**

1. Keep Subplan 4 open only for Doom95 validation.
2. Then do Subplan 6 or 5 depending on the first runtime failure after DDraw
   import exposure in the real game.
3. Treat Subplan 7 as runtime-driven integration work, not a fixed up-front
   feature bundle.

**Reference docs** (use selectively):
- `by_dll/*.md` — imported API inventory and notes; re-verify against current code
- `reference/render_backend.md` — backend interface notes
- `reference/init_sequence.md` — historical Doom95 startup sequence
- `reference/loader_notes.md` — loader/import-table change notes
