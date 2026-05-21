# DOOM95 Debug Log

## Scope

Compact resume state for `samples/unpacked/doom95/DOOM95.EXE`.
Keep only facts that change the next debugging move.

## Current status

- Use `./my_wine32`, not `./my_wine`, when validating the newest 32-bit changes.
- `make my_wine32` succeeds.
- Doom95 installs a `WH_KEYBOARD` hook (`SetWindowsHookExA id=2`) after the
  `Doom95Class` window and DirectDraw setup; returning NULL from this hook path
  is a credible cause of "renders but keys are ignored."
- `src/msvcrt/user32_message.c` now stores a minimal `WH_KEYBOARD` hook and
  invokes it for key messages retrieved through `GetMessageA` / `PeekMessageA`.
- SDL keydown/up events are now latched into the backend async-key state via an
  event watch, so short key taps can be observed by `GetAsyncKeyState` low-bit
  semantics even after the key is released.
- The Docker debug image `my_wine-samples` can now build and debug `my_wine32` inside Ubuntu 22.04.
- Doom95 no longer dies in the old post-launcher crash sites.
- Doom95 no longer crashes on the post-palette `IDirectDrawPalette::SetEntries` call.
- Doom95 now reaches sustained frame production.
- Visual gameplay is confirmed under Xvfb/openbox using the Docker debug image.
- The current live behavior is:
  - launcher autostart runs
  - `DOOM1.WAD` is found
  - `Doom95Class` window is created
  - DirectDraw setup gets much farther than before
  - `CreatePalette` and `SetPalette` both complete
  - `PaletteSetEntries` completes
  - `Lock` / `Unlock`, `Flip`, and occasional `Blt` continue steadily
  - `doom95_window.png` captures live gameplay
  - process stays alive and spins hot, dominated by `timeGetTime` polling

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
- `IDirectDrawPalette` vtable layout was missing `GetCaps` and `Initialize`; Doom95 calls
  `SetEntries` at vtable offset `0x18`, so the old 5-slot layout jumped through NULL.
- These fixes were necessary to get past:
  - crash immediately after `DirectDrawCreate`
  - crash after primary-surface setup
  - early `Couldn't set up screen!` failure
  - null call after `CreatePalette` / `SetPalette` at guest return site near `0x0043c41e`

### 3. The current run is beyond the old fatal setup error

Current debug trace from `./my_wine32` shows:

- `ddraw: DirectDrawCreate guid=(nil) -> ...`
- `ddraw: SetCooperativeLevel hwnd=0x41 flags=0x55 rb_window=23`
- `ddraw: SetCooperativeLevel hwnd=0x41 flags=0x51 rb_window=23`
- `ddraw: SetDisplayMode 640x400x8 exclusive=1 rb_window=23`
- `ddraw: CreateSurface caps=0x4218 size=640x400 backbuffers=2`
- `ddraw: CreateSurface caps=0x40 size=640x400 backbuffers=2`
- `ddraw: CreatePalette flags=0x44 count=256 -> ...`
- `ddraw: SetPalette count=1`
- `ddraw: PaletteSetEntries count=1`
- `ddraw: Lock count=...`
- `ddraw: Unlock count=...`
- `ddraw: Flip count=...`
- `winmm: timeGetTime count=...`

After that, Doom95 remains alive, continues producing DDraw activity, and presents visible gameplay frames.

## Visual confirmation

- Rebuilt the updated Docker image:
  - `DOCKER_BUILDKIT=0 docker build -t my_wine-samples .`
- Rebuilt the 32-bit binary inside the image to avoid host/container glibc mismatch:
  - `docker run --rm -v "$PWD:/project" -w /project my_wine-samples bash -lc 'make -B my_wine32'`
- Captured visual output under Xvfb/openbox:
  - `doom95_window.png`
  - `doom95_root.png`
- The captured frame shows live in-game Doom rendering.
- The prior black frame was not missing game drawing. The backbuffer had nonzero indexed pixels and a valid palette, but presentation used `SDL_SoftStretch` from `INDEX8` to the Xvfb `RGB888` window surface. SDL rejected that path with:
  - `Only works with same format surfaces`
  - then `Blit combination not supported`
- `src/backend/sdl2/rb_surface.c` now converts indexed frames to the window format before scaling/presenting.
- `MY_WINE_DEBUG_LEVEL=1` no longer enables the `SIGALRM` sampler; the sampler is level 2+ only, so level 1 traces are usable for normal Doom95 debugging.

## Current blocker

The blocker is no longer loader corruption, launcher state, first DirectDraw object creation, palette setup, or visual presentation.

The latest input-focused run proved:

- Doom95 polls `GetAsyncKeyState(VK_ESCAPE)` once, but live gameplay input is
  more likely through its `WH_KEYBOARD` hook.
- Doom95 reaches the live `Doom 95` window and then installs `WH_KEYBOARD`.
- Synthetic `xdotool` key events under Xvfb did not visually open the menu in
  the screenshot smoke test, so real interactive keyboard validation is still
  needed.
- CPU remains hot because Doom95 calls `timeGetTime` extremely aggressively.

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
  - `GetVerticalBlankStatus`
  - `IDirectDrawPalette::SetEntries`
  - `Lock` / `Unlock`
  - `Flip`
  - `Blt`
- A fresh instrumented run showed sustained post-palette activity:
  - `PaletteSetEntries count=2`
  - `Flip count=512+`
  - `Lock count=2048+`
  - `Unlock count=2048+`
  - `timeGetTime count=100,000,000+`
- A Docker/Xvfb capture showed visible gameplay and successful presentation:
  - `SDL_PIXELFORMAT_INDEX8` backbuffer
  - `SDL_PIXELFORMAT_RGB888` window surface
  - converted/scaled present returns `0`
  - screenshot contains 173 colors
- `PeekMessageA` idle polling rises slowly compared with the timer storm.
- A fallback backbuffer path was added in `src/msvcrt/ddraw_interface.c` so Doom95 can keep going even if the backend flip-chain state is incomplete.
- `gdb` batch launch works in the current environment and is useful for reproducing Doom95 behavior; plain sandboxed `./my_wine32` may still exit with code `159` before userland output.

## Best next move

Treat this as a hot timing/performance issue, not a startup-crash or render-confirmation bug.

### Priority 1: throttle or explain the `timeGetTime` storm

- Consider a narrow compatibility throttle in `timeGetTime` only after repeated same-millisecond polls.
- Keep it conservative; Doom95 is producing frames, so do not perturb timing heavily.
- Re-run with `MY_WINE_DEBUG_LEVEL=1` only briefly because timer logging becomes enormous.

The next session should answer:

- does a small yield in the timer poll reduce CPU without breaking frame production?
- is the apparent hot loop just Doom95's normal uncapped busy wait?

### Priority 2: clean up stale DDraw harnesses

- `samples/ddraw_sample_32` still carries an old local `IDirectDraw`/surface ABI and is not a reliable Doom95 verifier.
- `tests/test_ddraw.c` now builds after using the SDK-style `Lock` descriptor result, but its expectations still expose older DDSURFACEDESC/backend assumptions.

## What to avoid

- Do not spend more time on the old launcher invalid-control theory unless a fresh trace points back there.
- Do not reopen the old `0x004450cb` crash analysis unless the code regresses to that path.
- Do not broaden into unrelated DLL/import work without a new trace proving it matters.
- Do not revert the palette vtable fix; Doom95 directly needs the 7-slot palette ABI.

## Most relevant files

- [src/loader/image_mapper.c](/home/arzad/Playground/projects/my_wine/src/loader/image_mapper.c)
- [include/ddraw_types.h](/home/arzad/Playground/projects/my_wine/include/ddraw_types.h)
- [src/msvcrt/ddraw_interface.c](/home/arzad/Playground/projects/my_wine/src/msvcrt/ddraw_interface.c)
- [src/backend/sdl2/rb_surface.c](/home/arzad/Playground/projects/my_wine/src/backend/sdl2/rb_surface.c)
- [src/msvcrt/kernel32_doom95.c](/home/arzad/Playground/projects/my_wine/src/msvcrt/kernel32_doom95.c)
- [src/msvcrt/user32_dialog.c](/home/arzad/Playground/projects/my_wine/src/msvcrt/user32_dialog.c)
