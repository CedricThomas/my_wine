# DOOM95 Debug Log

## Scope

Compact resume state for `samples/unpacked/doom95/DOOM95.EXE`.
Keep only facts that change the next debugging move.

## Current status

- Use `./my_wine32`, not `./my_wine`, when validating the newest 32-bit changes.
- `make my_wine32` succeeds.
- Doom95 no longer dies in the old post-launcher crash sites.
- Doom95 still does not reach confirmed gameplay.
- The current live behavior is:
  - launcher autostart runs
  - `DOOM1.WAD` is found
  - `Doom95Class` window is created
  - DirectDraw setup gets much farther than before
  - `CreatePalette` and `SetPalette` both complete
  - process stays alive and spins hot instead of exiting cleanly or entering obvious gameplay

## Most important changes already proved

### 1. Loader corruption bug is fixed

- Doom95 had a PE section layout where a `.bss`-like section used:
  - nonzero `SizeOfRawData`
  - `PointerToRawData == 0`
- The old loader copied file bytes into that section and corrupted runtime state before guest code ran.
- `src/loader/image_mapper.c` now:
  - skips raw-copy for sections with `PointerToRawData == 0`
  - re-zeroes fileless/uninitialized sections after header copy
- Result:
  - the old corrupted-state crash path is gone

### 2. DirectDraw COM ABI was wrong and is now much closer to real

- `IDirectDraw` vtable layout in `include/ddraw_types.h` was wrong.
- `IDirectDrawSurface` vtable layout was also wrong.
- `DDSCAPS_*` values were wrong for the flags Doom95 uses.
- `DDSURFACEDESC` handling in `src/msvcrt/ddraw_interface.c` was reading the guest descriptor incorrectly.
- These fixes were necessary to get past:
  - crash immediately after `DirectDrawCreate`
  - crash after primary-surface setup
  - early `Couldn't set up screen!` failure

### 3. The current run is beyond the old fatal setup error

Current debug trace from `./my_wine32` shows:

- `ddraw: DirectDrawCreate guid=(nil) -> ...`
- `ddraw: SetCooperativeLevel hwnd=0x41 flags=0x55 rb_window=23`
- `ddraw: SetCooperativeLevel hwnd=0x41 flags=0x51 rb_window=23`
- `ddraw: SetDisplayMode 640x400x8 exclusive=1 rb_window=23`
- `ddraw: CreateSurface caps=0x4218 size=640x400 backbuffers=2`
- `ddraw: CreateSurface caps=0x40 size=640x400 backbuffers=2`
- `ddraw: CreatePalette flags=0x44 count=16 -> ...`
- `ddraw: SetPalette count=1`

After that, Doom95 remains alive instead of crashing.

## Current blocker

The blocker is no longer loader corruption, launcher state, or the first DirectDraw object creation.

The blocker is:

- Doom95 gets through major DirectDraw setup plus first palette attach
- then the process keeps running at high CPU
- gameplay is not yet confirmed
- short live sampling does not show a sustained post-palette storm in:
  - `IDirectDrawSurface::Flip`
  - `IDirectDrawSurface::Lock` / `Unlock`
  - `PeekMessageA`
  - `timeGetTime`
  - `GetTickCount`
- the remaining likelihood is shifting toward:
  - guest Doom95 code spinning after setup
  - an uninstrumented imported API path
  - a render/backend issue that occurs after only a few initial DDraw calls

## Important confirmed facts

- WAD discovery is no longer the problem.
- The launcher autostart path is good enough to reach the real game window.
- The old `0x004450cb` crash is obsolete for the current tree.
- Doom95 now calls at least these DDraw methods on the live path:
  - `DirectDrawCreate`
  - `SetCooperativeLevel`
  - `SetDisplayMode`
  - `CreateSurface`
  - `GetCaps`
  - `GetAttachedSurface`
  - `CreatePalette`
  - `SetPalette`
- A fresh instrumented run showed only a small early burst before the hot spin:
  - `GetAttachedSurface count=1`
  - `Flip count=4`
  - `Lock count=2`
  - `Unlock count=2`
  - `timeGetTime count=1`
- `PeekMessageA` idled a few times during launcher teardown, but no large post-start message-pump storm was observed.
- A fallback backbuffer path was added in `src/msvcrt/ddraw_interface.c` so Doom95 can keep going even if the backend flip-chain state is incomplete.
- `ptrace` attach with `gdb` is blocked in this environment (`Operation not permitted`), so the next move cannot depend on live debugger attach unless that restriction changes.

## Best next move

Treat this as a spinning-runtime bug after palette attach, not a startup-crash bug.

### Priority 1: broaden in-process sampling around the post-palette gap

- Run `./my_wine32 samples/unpacked/doom95/DOOM95.EXE`
- keep the new low-overhead counters enabled
- add the next narrow counters/logs to APIs that can still explain a hot spin:
  - `Blt` / `BltFast`
  - `GetFlipStatus` / `GetBltStatus`
  - `WaitForSingleObject` / `Sleep(0)` / sync waits
  - `QueryPerformanceCounter` or other timing APIs if present
- if needed, add one in-process guest-PC sampler instead of relying on external `gdb`

The next session should answer:

- is Doom95 spinning in guest code without frequent imported calls?
- is it polling an uninstrumented timing or wait API?
- does it switch to `Blt`/status polling after the first palette attach?

### Priority 2: verify the backbuffer path is actually usable after `SetPalette`

- recheck whether the primary surface has a valid attached backbuffer object on the live path
- if needed, add narrow logs for:
  - `surface_Blt`
  - `surface_GetFlipStatus`
  - `surface_GetBltStatus`

The goal is to confirm whether frames are being produced but not presented, or not produced at all.

### Priority 3: check uninstrumented wait/timer behavior

- if the hot loop is not in the extra DDraw/status logs, inspect:
  - kernel wait/sync stubs
  - timing APIs beyond `timeGetTime` / `GetTickCount`
  - focus/active-window assumptions
  - keyboard/input polling paths

## What to avoid

- Do not spend more time on the old launcher invalid-control theory unless a fresh trace points back there.
- Do not reopen the old `0x004450cb` crash analysis unless the code regresses to that path.
- Do not broaden into unrelated DLL/import work without a new trace proving it matters.

## Most relevant files

- [src/loader/image_mapper.c](/home/arzad/Playground/projects/my_wine/src/loader/image_mapper.c)
- [include/ddraw_types.h](/home/arzad/Playground/projects/my_wine/include/ddraw_types.h)
- [src/msvcrt/ddraw_interface.c](/home/arzad/Playground/projects/my_wine/src/msvcrt/ddraw_interface.c)
- [src/msvcrt/kernel32_doom95.c](/home/arzad/Playground/projects/my_wine/src/msvcrt/kernel32_doom95.c)
- [src/msvcrt/user32_dialog.c](/home/arzad/Playground/projects/my_wine/src/msvcrt/user32_dialog.c)
