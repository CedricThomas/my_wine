# DOOM95 on my_wine — Documentation Index

> Status: mixed.
>
> The subplans were re-audited against the current tree and can be used as a
> current Doom95 work plan. The lower-level by-DLL/reference notes are still
> mixed planning/reference material and may not exactly match the maintained
> source layout. For active runtime architecture, see `docs/architecture.md`,
> `docs/PE32.md`, and `audit/architecture-boundaries.md`.
>
> DirectDraw is now partially promoted from planning to maintained code: the
> narrowed DDraw path exists in source/tests/samples, and the remaining DDraw
> work is real-game validation against Doom95.

## Quick Lookup

To implement function `X` from DLL `Y`:

1. **Read `by_dll/Y.md`** → find the function row → get category + SDL2 mapping + notes
2. **Read `reference/file_layout.md`** only as a rough historical map; prefer the
   current source tree if it disagrees
3. **Read `subplans/subplan_NN_*.md`** → find the specific task checklist

---

## By-DLL Function Reference

Each file lists all imported functions for one DLL, grouped by subcategory.
Includes SDL2 mapping and implementation notes in a compact table format.

| File | DLL | Functions |
|------|-----|-----------|
| [by_dll/kernel32.md](by_dll/kernel32.md) | kernel32.dll | 69 unique (memory, file I/O, threading, TLS, system info, resources, etc.) |
| [by_dll/user32.md](by_dll/user32.md) | user32.dll | 56 unique (window, message, dialog, input) |
| [by_dll/gdi32.md](by_dll/gdi32.md) | gdi32.dll | 16 (DC, bitmap, palette, font, text) |
| [by_dll/ddraw.md](by_dll/ddraw.md) | ddraw.dll | 1 factory + ~42 vtable methods |
| [by_dll/dsound.md](by_dll/dsound.md) | dsound.dll | 1 factory + ~26 vtable methods + mixer |
| [by_dll/winmm.md](by_dll/winmm.md) | winmm.dll | 15 (timer, joystick, MIDI) |
| [by_dll/advapi32.md](by_dll/advapi32.md) | advapi32.dll | 5 (registry) |
| [by_dll/dplay.md](by_dll/dplay.md) | dplay.dll | 1 ordinal (multiplayer) |

---

## Reference

Self-contained extracts from the original large documents. Each 50–150 lines.

| File | Content |
|------|---------|
| [reference/render_backend.md](reference/render_backend.md) | `render_backend.h` interface spec |
| [reference/file_layout.md](reference/file_layout.md) | Project file layout + size estimates |
| [reference/milestones.md](reference/milestones.md) | 8 implementation milestones |
| [reference/risks.md](reference/risks.md) | 5 risks + mitigations |
| [reference/init_sequence.md](reference/init_sequence.md) | DOOM95 init sequence (from runtime analysis) |
| [reference/rendering.md](reference/rendering.md) | Surface hierarchy, rendering pattern, resolution |
| [reference/audio_config.md](reference/audio_config.md) | Audio subsystem details (DirectSound + MIDI) |
| [reference/loader_notes.md](reference/loader_notes.md) | What changes for new DLLs + loader architecture |

---

## Current Subplans

| File | Content |
|------|---------|
| [subplan_00_tracking.md](subplan_00_tracking.md) | Current Doom95 subplan index and status |
| [subplans/subplan_01_foundation.md](subplans/subplan_01_foundation.md) | Phase 1: Foundation |
| [subplans/subplan_02_sdl2_backend.md](subplans/subplan_02_sdl2_backend.md) | Phase 2: SDL2 Backend |
| [subplans/subplan_03_windowing.md](subplans/subplan_03_windowing.md) | Phase 3: Windowing |
| [subplans/subplan_04_ddraw.md](subplans/subplan_04_ddraw.md) | Phase 4: DirectDraw |
| [subplans/subplan_05_dsound.md](subplans/subplan_05_dsound.md) | Phase 5: DirectSound |
| [subplans/subplan_06_system_apis.md](subplans/subplan_06_system_apis.md) | Phase 6: System APIs |
| [subplans/subplan_07_integration.md](subplans/subplan_07_integration.md) | Phase 7: Integration |

---

## Legacy

Kept for reference only — the content has been redistributed above.
