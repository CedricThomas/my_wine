# Implementation Milestones

Each milestone is independently testable.

---

## Milestone 1: Window Creation

**Goal:** my_wine can create an SDL2 window from `CreateWindowExA`

**New files:**
- `src/stubs/user32_window.c` (partial: `CreateWindowExA`, `ShowWindow`, `SetWindowTextA`)
- `src/backend/render_backend_sdl2.c` (partial: `rb_init`, `rb_window_create`, `rb_window_show`, `rb_window_set_title`, `rb_shutdown`)
- `include/render_backend.h` (window functions only)
- `include/user32_types.h` (HWND, WNDCLASSA, RECT)

**New functions (8):**
`CreateWindowExA`, `ShowWindow`, `SetWindowTextA`, `DestroyWindow`, `RegisterClassA`, `GetWindowLongA`, `SetWindowLongA`, `IsWindow`

**Test:** Run a minimal PE test program that calls `CreateWindowExA` → `ShowWindow` → `Sleep(2000)` → `DestroyWindow`. Confirm SDL2 window appears with correct title and dimensions.

---

## Milestone 2: Event Loop

**Goal:** my_wine can poll events via `GetMessageA`/`PeekMessageA` and dispatch to a `WNDPROC`

**New files:**
- `src/stubs/user32_message.c`
- `include/user32_types.h` (extend with MSG, WNDPROC)

**New functions (9):**
`GetMessageA`, `PeekMessageA`, `DispatchMessageA`, `PostMessageA`, `PostQuitMessage`, `SendMessageA`, `DefWindowProcA`, `CallWindowProcA`, `SetWindowsHookExA` (stub)

**Test:** Run a PE test program with a message loop: `while (GetMessage(&msg, 0, 0, 0)) { DispatchMessage(&msg); }`. Clicking/closing the SDL2 window should trigger `WM_CLOSE` → `WM_DESTROY` → `PostQuitMessage` → loop exit.

---

## Milestone 3: DirectDraw Interface + Surfaces

**Goal:** my_wine can create an `IDirectDraw` interface, set display mode, and create surfaces

**New files:**
- `src/stubs/ddraw_interface.c`
- `src/stubs/ddraw_surface.c` (partial: `CreateSurface`, `GetSurfaceDesc`)
- `include/ddraw_types.h`
- `src/backend/render_backend_sdl2.c` (extend: surface/flip/palette)

**New functions (18 + 24 = 42 vtable methods):**
`DirectDrawCreate` + all `IDirectDraw` vtable methods + `IDirectDrawSurface::CreateSurface`, `GetSurfaceDesc`, `Lock`, `Unlock`, `Restore`, `SetPalette`

**Test:** Run a PE test program that calls `DirectDrawCreate` → `SetCooperativeLevel` → `SetDisplayMode(320, 200, 8)` → `CreateSurface(320, 200, 8-bit)`. Confirm the surface is created with correct dimensions. Check `GetSurfaceDesc` returns valid pitch.

---

## Milestone 4: Surface Rendering (Lock → Write → Unlock → Flip)

**Goal:** my_wine can render a surface to the screen

**New files:**
- `src/stubs/ddraw_surface.c` (extend: `Blt`, `Flip`)
- `src/backend/render_backend_sdl2.c` (extend: `rb_surface_blt`, `rb_surface_flip`, palette)

**New functions (extending existing vtable methods):**
`IDirectDrawSurface::Blt` → `SDL_BlitSurface`
`IDirectDrawSurface::BltFast` → simplified blit
`IDirectDrawSurface::Flip` → `SDL_UpdateWindowSurface`
`IDirectDrawSurface::SetPalette` → `SDL_SetPaletteColors`

**Test:** Run a PE test program that:
1. Creates a flip chain (primary + 1 back buffer)
2. Locks the back buffer
3. Writes a checkerboard pattern (alternating colors)
4. Unlocks
5. Flips
Confirm the checkerboard is visible on the SDL2 window.

---

## Milestone 5: DirectSound Playback

**Goal:** my_wine can play a sound (DirectSound buffer Lock → write → Play)

**New files:**
- `src/stubs/dsound_interface.c`
- `src/stubs/dsound_buffer.c`
- `include/dsound_types.h`
- `src/backend/render_backend_sdl2.c` (extend: audio functions)

**New functions (12 + 12 = 24 vtable methods):**
`DirectSoundCreate` + `IDirectSound` vtable + `IDirectSoundBuffer` vtable + PCM mixer

**Test:** Run a PE test program that:
1. Calls `DirectSoundCreate`
2. Creates a 1-second sine-wave buffer at 22050 Hz, 16-bit stereo
3. Locks → writes sine wave → unlocks
4. Calls `Play()`
Confirm audio is heard.

---

## Milestone 6: DOOM95 Init Completes

**Goal:** DOOM95.EXE passes the init sequence (window created, DDraw init, DSound init, MIDI init) without crashing

**New files:**
- `src/stubs/winmm_timer.c`
- `src/stubs/winmm_joystick.c`
- `src/stubs/winmm_midi.c` (stub version — `midiStreamOpen` returns success but no playback)
- `src/stubs/advapi32_registry.c`
- `src/stubs/kernel32_extended.c` (file I/O, TLS, timing)
- `src/stubs/dplay_stub.c`

**New functions:** ~40 remaining kernel32, 5 advapi32, 15 winmm, 1 dplay

**Test:** Launch DOOM95.EXE. Confirm:
- No crash during init
- Window appears (may be empty)
- Init messages/errors don't crash (registry stubs return `ERROR_SUCCESS`)
- DirectPlay stub returns `DP_OK` or graceful error
- Program reaches the title screen or crashes with a known render issue (not an init crash)

---

## Milestone 7: DOOM95 Game Loop

**Goal:** DOOM95 game loop runs (events processed, frames rendered, sounds play)

**New files:**
- `src/stubs/user32_input.c` (complete: `GetAsyncKeyState`, `GetKeyboardState` mapping)
- `src/stubs/gdi32_surface.c` (complete: `StretchDIBits`, `CreateDIBitmap`)
- `src/stubs/gdi32_palette.c` (complete)

**New functions:** ~15 (input + GDI rendering)

**Test:** Launch DOOM95.EXE from title screen. Confirm:
- Game world is rendered (even if colors are wrong)
- Player movement works (keyboard input)
- Sound effects play
- No repeated crashes
- FPS is reasonable (≥ 15 fps is acceptable for first pass)

---

## Milestone 8: DOOM95 Menus / Dialogs

**Goal:** DOOM95 options menus, episode selection, and CD key dialogs work

**New files:**
- `src/stubs/user32_dialog.c` (complete)
- `include/dialog_types.h`
- `src/stubs/gdi32_font.c`

**New functions:** 7 (dialog system + font)

**Test:** From title screen, open options menu. Confirm:
- Dialog appears (even if controls don't render perfectly)
- Checkboxes and radio buttons can be toggled
- Menu can be closed
- Episode selection works

---

## Summary

| Milestone | New Files | New Functions | Test Method |
|-----------|-----------|---------------|-------------|
| 1: Window | 3 | 8 | Test PE with `CreateWindowExA` |
| 2: Events | 1 | 9 | Test PE with message loop |
| 3: DDraw Init | 3 | 42 | Test PE with DDraw surface creation |
| 4: Rendering | 0 (extend) | 4 | Test PE with Lock→Write→Flip |
| 5: Audio | 3 | 24 | Test PE with sine-wave buffer |
| 6: DOOM95 Init | 6 | ~40 | Launch DOOM95.EXE, no crash |
| 7: Game Loop | 3 | ~15 | DOOM95 renders + moves |
| 8: Dialogs | 2 | 7 | DOOM95 menus work |
