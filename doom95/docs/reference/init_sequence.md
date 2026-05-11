# DOOM95 Init Sequence

**Binary:** `DOOM95.EXE` (775,117 bytes)
**Format:** PE32 executable, Intel i386, compiled with **Watcom C/C++ 3.1**
**Subsystem:** Windows GUI (subsystem 2)
**Entry Point:** RVA `0x000444d8`
**Image Base:** `0x00400000`

## Entry Point (VMA 0x004444d8, file offset 0x0348d8)

```
004444d8:  mov     dword ptr [0x618364], 0x43a1f0    ; Store pointer to CRT data
004444e2:  jmp     0x00447477                          ; Jump to Watcom CRT startup
```

## Watcom CRT Startup (VMA 0x00447477)

```
push    ebx; push ecx; push edx; push ebp; mov ebp, esp
sub     esp, 8
mov     eax, 1              ; CRT mode flag
call    0x004481d8           ; CRT initialization (env/argv parsing)
; ... BSS zeroing, heap init ...
call    0x0044887d           ; FP precision setup (sets 0x477d74 = 0x8000)
call    0x00449d6c           ; >>> Jump table to D_DoomMain <<<
```

## CRT Jump Table (VMA 0x00449d6c)

The jump table dispatches to user code via 16 indirect jumps through the IAT. The first entry (`jmp dword ptr [0x62058c]`) resolves to **`D_DoomMain`**. This is the Watcom-compiled entry point mechanism — the CRT calls the user's `main` through a resolved IAT thunk.

## D_DoomMain Init Sequence

Inferred from imported functions and error message strings:

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
13. **`D_DoomLoop`** — Main game loop

**Order:** Window → DDraw → DSound → Game Loop

The init order is confirmed by error strings appearing in sequence:
- "DirectDrawCreate failed"
- "Couldn't create primary flipping surface"
- "Sound buffer lock failure!"
