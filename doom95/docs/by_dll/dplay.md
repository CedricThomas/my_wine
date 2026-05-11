# dplay.dll — 1 ordinal import

## Multiplayer (1)

| Function | Category | SDL2 Mapping | Notes |
|----------|----------|-------------|-------|
| `Ordinal #1` (= `DPCreate`) | optional | Stub (return DP_OK/NULL) | DirectPlay 1.x factory. Returns mock `IDirectPlay*` with no-op methods. |

**Context:**
- DOOM95 uses DirectPlay for network multiplayer and XBAND online play
- The game has graceful degradation — error strings show `"DirectPlayCreate Failed!"` and `"DirectPlay Open Failed!"`
- Single-player mode works without DirectPlay
- Strings reference `XBSendPlayerData` for XBAND support

**Stub approach:**
- `DPCreate` returns a mock `IDirectPlay` struct of function pointers
- All methods return `DP_OK` or no-op
- File: `src/stubs/dplay_stub.c` (~50 lines)
