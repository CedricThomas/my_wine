# DOOM95 Implementation Tracking

> Status: historical planning reference.
>
> These subplans predate later PE32/runtime cleanup. Treat them as old planning
> notes, not current work tracking. Re-audit current source boundaries before
> using any file paths or estimates below.

7 independent subplans. One per session. Check off as you go.

| # | Subplan | Files | ~Lines | ~Days | Status |
|---|---------|-------|--------|-------|--------|
| 1 | [Foundation](subplans/subplan_01_foundation.md) | 16 | ~3,670 | 3-4 | ✅ Done |
| 2 | [SDL2 Backend](subplans/subplan_02_sdl2_backend.md) | 2 | ~2,000 | 8-10 | ☐ |
| 3 | [Windowing](subplans/subplan_03_windowing.md) | 5 | ~950 | 4-5 | ☐ |
| 4 | [DirectDraw](subplans/subplan_04_ddraw.md) | 6 | ~960 | 4-5 | ☐ |
| 5 | [DirectSound](subplans/subplan_05_dsound.md) | 4 | ~600 | 3 | ☐ |
| 6 | [System APIs](subplans/subplan_06_system_apis.md) | 8 | ~1,080 | 4-5 | ☐ |
| 7 | [Final Integration](subplans/subplan_07_integration.md) | 5 | ~930 | 4-5 | ☐ |
| **Total** | | **~46** | **~10,190** | **~30-38** | |

**Reference docs** (read as needed, not loaded into context):
- `by_dll/*.md` — all import functions + SDL2 mappings, one file per DLL (8 files, 542 lines total)
- `reference/render_backend.md` — render_backend.h interface
- `reference/file_layout.md` — project file layout + size estimates
- `reference/milestones.md` — 8 implementation milestones
- `reference/risks.md` — 5 risks + mitigations
- `reference/init_sequence.md` — DOOM95 init sequence
- `reference/rendering.md` — surface hierarchy, rendering pattern
- `reference/audio_config.md` — audio subsystem details
- `reference/loader_notes.md` — loader architecture + what changes for new DLLs
