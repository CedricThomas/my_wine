# DOOM95 Subplan Tracking

7 independent subplans. One per session. Check off as you go.

| # | Subplan | Files | ~Lines | ~Days | Status |
|---|---------|-------|--------|-------|--------|
| 1 | [Foundation](subplans/subplan_01_foundation.md) | 10 | ~870 | 3-4 | ☐ |
| 2 | [SDL2 Backend](subplans/subplan_02_sdl2_backend.md) | 2 | ~2,000 | 8-10 | ☐ |
| 3 | [Windowing](subplans/subplan_03_windowing.md) | 5 | ~950 | 4-5 | ☐ |
| 4 | [DirectDraw](subplans/subplan_04_ddraw.md) | 6 | ~960 | 4-5 | ☐ |
| 5 | [DirectSound](subplans/subplan_05_dsound.md) | 4 | ~600 | 3 | ☐ |
| 6 | [System APIs](subplans/subplan_06_system_apis.md) | 8 | ~1,080 | 4-5 | ☐ |
| 7 | [Final Integration](subplans/subplan_07_integration.md) | 5 | ~930 | 4-5 | ☐ |
| **Total** | | **~40** | **~7,390** | **~30-38** | |

**Reference docs** (read as needed, not loaded into context):
- `imports.md` — 164 DOOM95 import functions
- `sdl2_mapping.md` — SDL2 equivalent for every import
- `loader_analysis.md` — my_wine loader architecture
- `runtime_analysis.md` — DOOM95 init sequence, rendering, audio, game loop
- `scope.md` — complete architecture (render_backend.h interface, file layout, risks)
