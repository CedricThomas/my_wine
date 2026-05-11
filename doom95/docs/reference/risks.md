# Risks and Mitigations

---

## Risk 1: PE32 (32-bit) Incompatibility — **HIGH likelihood, CRITICAL impact**

**The gap:** DOOM95 is PE32 (i386). my_wine currently only supports PE32+ (x64). The import table uses `IMAGE_THUNK_DATA64`, PEB offsets are for x64, and thunks are x86_64 machine code.

**Mitigation options** (in order of preference):
1. **Shim DOOM95 through a 32-bit compatibility layer**: Add `IMAGE_THUNK_DATA32` support to the import parser. This is the minimal change — the PE32 vs PE32+ difference is mostly in thunk size (4 bytes vs 8 bytes) and header layout. The stub functions still execute as x64 on the host, so only the PE parser needs PE32 awareness. **Estimated effort: 200 lines, 2 days.**
2. **Full PE32 loader**: Add complete PE32 section mapping, 32-bit relocation handling, and 32-bit thunk generation. **Estimated effort: 1,000 lines, 1–2 weeks.**
3. **Recompile DOOM95 as x64**: Use MinGW-w64 or DOSBox-X's i386 compatibility layer to produce a PE32+ binary. This is high-risk — Watcom-compiled code has inline assembly and ABI-specific assumptions.

**Recommendation:** Option 1 — add PE32 thunk/image support to the parser only. The stub functions remain x64. The key changes are in `pe_imports.c` (read 32-bit thunks) and `teb_peb.c` (32-bit image base offsets). This is the path of least resistance.

---

## Risk 2: Watcom CRT vs mingw-w64 CRT — **MEDIUM likelihood, HIGH impact**

**The gap:** DOOM95 is compiled with Watcom C/C++ 3.1. The CRT startup sequence (`__watcom_startup` → env parsing → heap init → `D_DoomMain`) is completely different from mingw-w64's `__getmainargs` path. my_wine currently patches mingw-w64-specific `.rdata` refptrs.

**Impact:** If the CRT startup code calls into `kernel32.dll` or `msvcrt.dll` functions we haven't stubbed, it will crash before `D_DoomMain` is reached. The entry point is at `0x004444d8` → jumps to Watcom CRT → eventually calls `D_DoomMain`.

**Mitigation:**
1. **Trace the Watcom CRT init path** and stub any Windows API calls it makes (likely minimal — just heap/init).
2. **Bypass CRT entirely**: Set the entry point to `D_DoomMain` directly (RVA can be resolved from the jump table at `0x00449d6c`). This skips the CRT's env/argv parsing. The game reconstructs command-line args via `GetCommandLineA`.
3. **Pre-seed the Watcom CRT globals** (the `0x618364` pointer stored at entry) to valid values.

**Recommendation:** Option 2 — bypass Watcom CRT and jump directly to `D_DoomMain`. The game uses `GetCommandLineA` for args (which we stub) and `GlobalAlloc`/`LocalAlloc` for heap (which we map to `malloc`). This avoids CRT compatibility entirely.

---

## Risk 3: DirectDraw Surface Format Assumptions — **HIGH likelihood, MEDIUM impact**

**The gap:** DOOM95 renders at 320×200 with 8-bit (256-color) palettized surfaces. SDL2's `SDL_CreateRGBSurface()` supports 8-bit with palette, but the pitch/stride may differ from what DOOM95 expects. DDraw surfaces may assume row-aligned pitch (e.g., 320 bytes per row for 8-bit), but SDL2 may pad to a larger alignment.

**Impact:** If pitch is wrong, DOOM95's `Lock`-write-`Unlock` path will write to wrong memory offsets → visual corruption.

**Mitigation:**
1. **In `rb_surface_lock`**, return a custom-pitched buffer that matches DOOM95's expected pitch. Copy from/to the SDL surface as needed.
2. **Force SDL surface pitch** by allocating with exact pitch and zero-filling padding bytes (may require `SDL_CreateRGBSurfaceWithFormatFrom` with a manually-allocated buffer).
3. **Accept visual corruption in edge cases** and fix only when DOOM95 actually surfaces the issue.

**Recommendation:** Option 2 — allocate buffers with exact expected pitch. For 320×200 8-bit, that's 320 bytes per row, 64000 bytes total. Allocate with `malloc` and wrap in `SDL_CreateRGBSurfaceFrom()`.

---

## Risk 4: Dialog Rendering — **MEDIUM likelihood, LOW impact**

**The gap:** DOOM95 uses Windows-native GDI dialogs for menus. These dialogs rely on `DefWindowProcA`, control drawing (buttons, checkboxes, static text), and palette management. If we stub `CreateDialogParamA` to just create an empty `HWND`, the dialog won't render.

**Impact:** Game menus won't work. DOOM95 may crash trying to access dialog controls. However, the in-game HUD is rendered on the DDraw surface (not via GDI), so gameplay is unaffected.

**Mitigation:**
1. **Minimal dialog support**: Parse dialog resources, create mock controls, respond to `CheckDlgButton`/`IsDlgButtonChecked` in memory. Let `DefWindowProcA` return 0. The dialog won't visually render, but button state changes will be tracked.
2. **SDL2-based dialog rendering**: Draw controls using `SDL_BlitSurface` (text + rectangles). High effort, low payoff.
3. **Skip dialogs entirely**: Hard-code the game to skip dialog creation and use a text-mode config instead.

**Recommendation:** Option 1 — track dialog control state in memory. Dialogs won't visually render, but the game won't crash. Dialogs can be polished in a later pass.

---

## Risk 5: MIDI Playback — **MEDIUM likelihood, LOW impact**

**The gap:** DOOM95 uses `midiStreamOpen` + `midiStreamOut` for music. SDL2 has no MIDI support. Stubbing means silent music.

**Impact:** No background music. Sound effects (DirectSound) still work. Low impact on playability.

**Mitigation:**
1. **Stub (phase 1)**: `midiStreamOpen` returns a valid handle. `midiStreamOut` is a no-op. Game runs silent for music.
2. **libmodplug/Timidity++ (phase 2)**: Link one of these libraries. `midiStreamOpen` opens a synth instance. `midiStreamOut` feeds MIDI bytes to the synth.
3. **SDL_mixer (phase 2)**: `Mix_OpenAudio()` + `Mix_PlayMusic()` with MOD support.

**Recommendation:** Option 1 for initial milestone. Add Option 2 or 3 in a follow-up.
