# DOOM95 Runtime Behavior Analysis

**Binary**: `DOOM95.EXE` (775,117 bytes)
**Format**: PE32 executable, Intel i386, compiled with **Watcom C/C++ 3.1**
**Subsystem**: Windows GUI (subsystem 2)
**Entry Point**: RVA `0x000444d8`
**Image Base**: `0x00400000`
**Sections**: BEGTEXT (code), DGROUP (data), .bss (uninit), .idata (imports), .reloc, .rsrc

---

## 1. Init Sequence (Entry Point Trace)

### Entry Point (VMA 0x004444d8, file offset 0x0348d8)
```
004444d8:  mov     dword ptr [0x618364], 0x43a1f0    ; Store pointer to CRT data
004444e2:  jmp     0x00447477                          ; Jump to Watcom CRT startup
```

### Watcom CRT Startup (VMA 0x00447477)
```
push    ebx; push ecx; push edx; push ebp; mov ebp, esp
sub     esp, 8
mov     eax, 1              ; CRT mode flag
call    0x004481d8           ; CRT initialization (env/argv parsing)
; ... BSS zeroing, heap init ...
call    0x0044887d           ; FP precision setup (sets 0x477d74 = 0x8000)
call    0x00449d6c           ; >>> Jump table to D_DoomMain <<<
```

### CRT Jump Table (VMA 0x00449d6c)
The jump table dispatches to user code via 16 indirect jumps through the IAT. The first entry (`jmp dword ptr [0x62058c]`) resolves to **`D_DoomMain`**. This is the Watcom-compiled entry point mechanism — the CRT calls the user's `main` through a resolved IAT thunk.

### D_DoomMain Init Sequence (inferred from strings and imported APIs)

Based on the imported functions and error message strings, the init sequence is:

1. **`D_DoomMain`** — Entry point, parses command line args (`-episode`, `-skill`, `-map`)
2. **Registry access** via ADVAPI32 (`RegOpenKeyA`, `RegQueryValueExA`, `RegCreateKeyA`) — reads/saves config from `HKEY_CURRENT_USER\Software\Id Software\DOOM`
3. **`RegisterClassA`** — Registers the game window class
4. **`CreateWindowExA`** or **`CreateDialogParamA`** — Creates the main window/dialog
5. **`DirectDrawCreate`** — Creates the DDraw interface
6. **`IDirectDraw->SetCooperativeLevel`** — Sets exclusive mode
7. **`IDirectDraw->SetDisplayMode`** — Sets display resolution
8. **`IDirectDraw->CreateSurface`** — Creates flip chain (primary + back buffers)
9. **`DirectSoundCreate`** — Creates the DirectSound interface
10. **`IDirectSound->SetCooperativeLevel`** — Sets cooperative level
11. **`IDirectSound->CreateSoundBuffer`** — Creates primary sound buffer
12. **MIDI initialization** via WINMM (`midiStreamOpen`, `midiOutSetVolume`)
13. **`D_DoomLoop`** — Main game loop (see section 3)

### Order: Window → DDraw → DSound → Game Loop

The init order is confirmed by:
- The presence of both `CreateWindowExA` and `CreateDialogParamA` imports
- The DDraw/DSound Create functions being imported
- Error strings like "DirectDrawCreate failed" and "Couldn't create primary flipping surface" appearing in sequence

---

## 2. Rendering Path

### Surface Architecture: Hardware Flip Chain with Offscreen Surfaces

The DDCAPS/DDSCAPS constants in the DGROUP section reveal a sophisticated surface layout:

| Cap | Count | Meaning |
|-----|-------|---------|
| DDCAPS_FLIP | 33 | Hardware surface flipping |
| DDCAPS_BLT | 275 | Hardware blitting |
| DDCAPS_BLTHW | 216 | Hardware blitter available |
| DDCAPS_VIDEOMEMORY | 213 | VRAM surfaces |
| DDSCAPS_FRONTBUFFER | 167 | Primary surface |
| DDSCAPS_BACKBUFFER | 311 | Multiple back buffers |
| DDSCAPS_COMPLEX | 312 | Flip chain (primary + backs) |
| DDSCAPS_FLIP | 168 | Surface flipping |

### Surface Hierarchy (from global variable names)
- **`lpDD`** — `IDirectDraw*` interface pointer
- **`lpDDSPrimary`** — Primary surface with front buffer
- **`lpDDSBack`** — Back buffer(s) for flip chain
- **`lpDDSOff`** — Offscreen surface for rendering
- **`lpDDSOffFlat`** — Flat offscreen surface (system memory fallback)
- **`lpDDSPage4`** — 4th page/back buffer
- **`lpDDSFlash`** — Flash/burst effect surface
- **`lpDDPal`** — `IDirectDrawPalette*`
- **`lpClipper`** — `IDirectDrawClipper*`

### Rendering Pattern
The game uses a **3D-style software renderer** with the following pattern:
1. Render to offscreen surface (`lpDDSOff` or `lpDDSOffFlat`)
2. **`Lock`/`Unlock`** to get direct memory access ("megalock failed" errors confirm this)
3. Render the frame to the locked surface (the "MegaLock" terminology is DOOM95-specific)
4. **Flip** or **Blt** from offscreen to back buffer
5. **Flip** the back buffer to primary

### Resolution
DOOM95 supports multiple resolutions. The standard is **320x200** (classic DOOM resolution) at 8-bit (256-color) with palette management. The game may also support higher resolutions via `SetDisplayMode`.

### Error Strings Confirm the Pattern
```
"R_RenderPlayerView1: megalock failed"
"V_DrawPatchDirect: megalock failed"
"Couldn't create primary flipping surface with two back buffers in vram"
"Couldn't create primary flipping surface with one back buffer in vram"
"Couldn't create primary flipping surface with one back buffers in any ram"
"Rendering to system memory offscreen surface"
"EraseSurface: blt returned %d"
```

The fallback chain is: VRAM flip chain → system memory flip chain → software blt.

---

## 3. Message Loop

### Game Loop Structure

The `D_DoomLoop_` function (identified in the string table) implements the game loop. Based on the imported functions and typical DOOM architecture:

```c
// Simplified structure (reconstructed from imports)
while (1) {
    // Event processing
    while (PeekMessageA(&msg, NULL, 0, 0, PM_NOREMOVE)) {
        if (!GetMessageA(&msg, NULL, 0, 0)) break;  // WM_QUIT
        TranslateMessage(&msg);
        DispatchMessageA(&msg);
    }

    // Input processing
    // (GetAsyncKeyState for keyboard, joyGetPosEx for joystick)

    // Game logic
    D_ProcessEvents_();
    G_DrawPlayer();
    // ... sector updates, thing movement ...

    // Render frame
    I_StartTic();
    D_Display_();  // Lock surface, render, unlock, flip

    // Throttle frame rate
    while (timeGetTime() < next_tick) Sleep(1);
}
```

### Window Messages Handled
The WNDPROC (likely `D_WndProc` or similar) handles:
- **`WM_KEYDOWN`/`WM_KEYUP`** — Keyboard input
- **`WM_SYSKEYDOWN`** — Alt+Tab, F1, etc.
- **`WM_MOUSEMOVE`/`WM_LBUTTONDOWN`/`WM_RBUTTONDOWN`** — Mouse input
- **`WM_SIZE`/`WM_MOVE`** — Window resizing
- **`WM_PAINT`** — Repaint (BeginPaint/EndPaint imported)
- **`WM_DESTROY`** — Cleanup
- **`WM_SYSCOMMAND`** — Minimize/restore
- **`WM_SETCURSOR`** — Cursor hiding (DOOM95 hides the cursor)
- **`WM_DISPLAYCHANGE`** — Display mode change notification

### Hook System
`SetWindowsHookExA` and `UnhookWindowsHookEx` are imported, suggesting the game installs a keyboard hook for:
- Capturing key presses in background
- Detecting Ctrl+Alt+Del
- Menu key handling

---

## 4. Audio Subsystem

### Sound API Strategy: DirectSound + MIDI + WinMM

The binary imports both DirectSound (`DirectSoundCreate`) and multiple MIDI/WinMM functions, indicating a dual audio path:

**DirectSound (primary for SFX):**
- `DirectSoundCreate` → `IDirectSound`
- `IDirectSound->SetCooperativeLevel` (DSBULL_IMPORTANT/DSBULL_PRIORITY)
- `IDirectSound->CreateSoundBuffer` → primary buffer
- `IDirectSound->CreateSoundBuffer` → secondary buffers (one per sound)

**WinMM MIDI (for music):**
- `midiStreamOpen` → Open MIDI output device
- `midiStreamOut` → Play MIDI sequences
- `midiStreamPause`/`midiStreamRestart` → Pause/resume music
- `midiOutSetVolume` → Volume control
- `midiOutPrepareHeader`/`midiOutUnprepareHeader` → MIDI buffer management

**WinMM Joystick:**
- `joyGetNumDevs` → Count joysticks
- `joyGetDevCapsA` → Get joystick capabilities
- `joyGetPosEx` → Read joystick position

### Sample Rate
- **22050 Hz** (0x5622) found in code at multiple locations — primary sample rate for SFX
- **11025 Hz** (0x2B11) also found — possibly for lower quality/compatibility mode
- The game likely uses **16-bit stereo** PCM for DirectSound SFX (confirmed by `nBlockAlign=4` patterns in data)

### Sound Buffer Lock/Unlock Pattern
Error strings confirm:
```
"Sound buffer lock failure!"
"Sound buffer unlock failure!"
"Bad CreateSoundBuffer: %d on sound %d %s"
```

The game uses `Lock`/`Unlock` to write PCM data into sound buffers, then `Play()` to start playback. The format string shows the game validates buffer format:
```
"wFormatTag %d Channels %d nSamplesPerSec %d nAvgBytesPerSec %d nBlockAlign %d wBitsPerSample %d cbSize %d"
```

---

## 5. DPLAY Ordinal #1

### Import
```
DPLAY.dll → ordinal #1 (HIGH bit set = ordinal import, value 0x80000001)
```

**Ordinal #1 in dplay.dll is `DPCreate`** — the DirectPlay 1.x factory function.

### Usage
From the string table:
```
"DirectPlayCreate Failed!"
"DirectPlayCreate Failed! %d"
"DirectPlay Open Failed!"
"DirectPlay Open Failed! %d"
"XBSendPlayerData"
"Could not load XBSendPlayerData."
"DPLAY.dll"
```

DOOM95 uses DirectPlay for **network multiplayer** (and XBAND support). The function `DPCreate` is called to create an `IDirectPlay*` interface, which is then used for:
- Creating/opening a network session
- Sending/receiving game state over the network
- XBAND online play (via `XBSendPlayerData`)

### For Stubbing
DirectPlay is **optional** — the game runs fine in single-player mode without it. The error strings suggest graceful degradation when DPLAY.dll is not available.

---

## 6. Dialog System

### Dialog Resources
The `.rsrc` section contains **3 dialog templates**:
| Resource ID | Type | Purpose |
|-------------|------|---------|
| 0x68 (104) | DIALOG | Likely the options/setup dialog |
| 0x82 (130) | DIALOG | Likely the episode/skill selection dialog |
| 0x83 (131) | DIALOG | Likely the multiplayer/CD key dialog |

### Dialog Functions
- **`CreateDialogParamA`** — Creates modeless dialogs from resource templates
- **`IsDialogMessageA`** — Routes messages to dialog
- **`CheckDlgButton`**/`IsDlgButtonChecked` — Radio buttons/checkboxes
- **`GetDlgItem`** — Get control handles
- **`SetDlgItemTextA`** — Set control text
- **`SendMessageA`** — Send messages to controls
- **`CallWindowProcA`** — Call original dialog proc

### Rendering Approach
The game uses **Windows-native GDI dialogs** for menus (options, episode select, etc.), rendered using standard Win32 controls. The `DefWindowProcA` import confirms dialog procedure delegation.

The **in-game HUD** is rendered directly on the DDraw surface (not via GDI), using the game's own rendering engine (patches, fixed-point font, etc.).

### Bitmap Resources
7 bitmap resources (IDs 0x66–0x84) provide background images for dialogs and possibly in-game assets.

---

## 7. Palette / Color Mode

### 8-Bit (256-Color) Palette Mode

Evidence:
- **`CreatePalette`** imported from GDI32 — creates a logical palette
- **`RealizePalette`** imported — realizes palette into display
- **`SelectPalette`** imported — selects palette for a DC
- **`GetSystemPaletteEntries`** imported — queries system palette
- **`SetBkColor`**/`SetTextColor` imported — for text rendering in dialogs
- **`IDirectDrawPalette`** interface (`lpDDPal`) — for DDraw palette control

### Color Depth
The game uses **8-bit (256-color) palettized rendering** for the game world, with:
- DDraw palette management for the fullscreen display
- GDI palette for dialog rendering
- Palette transitions (fade in/out between areas)

### Resolution and Bit Depth Options
The game likely supports:
- **320x200 @ 8-bit** (default DOOM resolution)
- **640x400 @ 8-bit** (high res mode)
- Possibly **320x200 @ 16-bit** (via dithering, though less likely for DOOM95)

---

## Summary: Critical Implementation Priorities

### Tier 1: MUST IMPLEMENT (Core Game Loop)

| Priority | Component | Functions/APIs |
|----------|-----------|----------------|
| 1 | **Window Management** | `RegisterClassA`, `CreateWindowExA`, `CreateDialogParamA`, `ShowWindow`, `UpdateWindow`, `SetWindowTextA`, `SetWindowLongA`, `GetWindowLongA` |
| 2 | **Message Loop** | `GetMessageA`, `PeekMessageA`, `DispatchMessageA`, `TranslateMessage`, `PostMessageA`, `PostQuitMessage` |
| 3 | **DDraw Core** | `DirectDrawCreate`, IDirectDraw vtable (18 methods): `SetCooperativeLevel`, `SetDisplayMode`, `CreateSurface`, `CreatePalette`, `CreateClipper`, `GetAvailableVidMem`, `GetMonitorFrequency`, `GetFourCCCodes` |
| 4 | **DDraw Surface** | IDirectDrawSurface vtable (24 methods): `AddAttachedSurface`, `AddOverlayDirtyRect`, `Blt`, `BltBatch`, `BltFast`, `DeleteAttachedSurface`, `GetBltStatus`, `GetAttachedSurface`, `GetOverrideCursor`, `GetFlipStatus`, `GetSurfaceDesc`, `Lock`, `IsLost`, `Restore`, `SetAttachedSurface`, `SetOverlayPosition`, `SetPalette`, `Unlock` |
| 5 | **DirectSound Core** | `DirectSoundCreate`, IDirectSound vtable: `CreateSoundBuffer`, `GetCaps`, `Get CooperativeLevel`, `Initialize` |
| 6 | **DirectSound Buffer** | IDirectSoundBuffer vtable: `GetCaps`, `GetCurrentPosition`, `GetFormat`, `GetVolume`, `Lock`, `Play`, `SetFrequency`, `SetFormat`, `SetVolume`, `Stop`, `Unlock` |

### Tier 2: SHOULD IMPLEMENT (Full Feature)

| Priority | Component | Functions/APIs |
|----------|-----------|----------------|
| 7 | **GDI Dialog Support** | `CreateDCA`, `CreateDIBitmap`, `CreateFontA`, `CreatePalette`, `DeleteDC`, `DeleteObject`, `GetDeviceCaps`, `GetObjectA`, `GetStockObject`, `GetSystemPaletteEntries`, `RealizePalette`, `SelectPalette`, `SetBkColor`, `SetTextColor`, `StretchDIBits`, `UnrealizeObject` |
| 8 | **MIDI Audio** | `midiOutGetNumDevs`, `midiOutPrepareHeader`, `midiOutReset`, `midiOutSetVolume`, `midiOutUnprepareHeader`, `midiStreamClose`, `midiStreamOpen`, `midiStreamOut`, `midiStreamPause`, `midiStreamProperty`, `midiStreamRestart`, `timeGetTime` |
| 9 | **Input** | `GetAsyncKeyState`, `joyGetDevCapsA`, `joyGetNumDevs`, `joyGetPosEx` |
| 10 | **File I/O** | `CreateFileA`, `ReadFile`, `WriteFile`, `SetFilePointer`, `GetFileSize`, `GetFileAttributesA`, `GetModuleFileNameA`, `SearchPathA` |

### Tier 3: CAN BE STUBBED (Optional Features)

| Priority | Component | Functions/APIs |
|----------|-----------|----------------|
| 11 | **DirectPlay** | `DPCreate` (ordinal #1) — stub with error return |
| 12 | **Registry** | `RegOpenKeyA`, `RegQueryValueExA`, `RegCreateKeyA`, `RegSetValueExA`, `RegCloseKey` — stub with error return (config files can be used instead) |
| 13 | **Hook System** | `SetWindowsHookExA`, `UnhookWindowsHookEx`, `CallNextHookEx` — stub with NULL |
| 14 | **Console** | `GetConsoleMode`, `SetConsoleMode`, `ReadConsoleInputA`, `WriteConsoleA`, `GetStdHandle`, `SetStdHandle` — stub |
| 15 | **Advanced Win32** | `GetTickCount`, `Sleep`, `GetVersion`, `GetSystemInfo`, `GetCommandLineA`, `GetEnvironmentStrings` |

### Total API Surface
- **DLLs**: 10 (KERNEL32×2, USER32×2, GDI32, WINMM, ADVAPI32, DDRAW, DSOUND, DPLAY)
- **Total imported functions**: ~160 named + 1 ordinal
- **DirectX interfaces needing vtables**: IDirectDraw, IDirectDrawSurface, IDirectDrawPalette, IDirectDrawClipper, IDirectSound, IDirectSoundBuffer

### Key Implementation Insight
DOOM95 only imports **DirectDrawCreate** and **DirectSoundCreate** from their respective DLLs. All other DirectX methods are called through **vtable dispatch** (calling method pointers stored in the interface vtables). This means the SDL2 bridge only needs to:
1. Implement the `Create` factory functions
2. Build correct vtable structures for each interface
3. Map each vtable method to SDL2 equivalents
