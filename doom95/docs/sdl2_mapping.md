# SDL2 API Mapping for DOOM95

Complete mapping of all 164 DOOM95 imports to their closest SDL2 equivalent, C standard library function, or required custom implementation.

---

## user32 → SDL2 Video / Events / Window

**56 unique functions** (54 from `user32.dll` + 2 from `user32.DLL`)

| Function | SDL2 Equivalent | Notes / Gap |
|----------|----------------|-------------|
| `CreateWindowExA` | `SDL_CreateWindow()` | Map `DWORD` style → `Uint32` flags (`SDL_WINDOW_SHOWN`, `SDL_WINDOW_RESIZABLE`, etc.). `HINSTANCE` and `LPVOID` create-params ignored. |
| `DestroyWindow` | `SDL_DestroyWindow()` | Trivial. Return `TRUE`. |
| `ShowWindow` | `SDL_ShowWindow()` / `SDL_HideWindow()` | `nCmdShow` SW_SHOW → `SDL_ShowWindow()`, SW_HIDE → `SDL_HideWindow()`. |
| `SetWindowPos` | `SDL_SetWindowPosition()` + `SDL_SetWindowSize()` | Split `x,y,w,h` into two calls. Ignore `uFlags` except SWP_NOSIZE/SWP_NOMOVE. |
| `MoveWindow` | `SDL_SetWindowPosition()` + `SDL_SetWindowSize()` | Same split. |
| `SetWindowTextA` | `SDL_SetWindowTitle()` | Trivial string copy. |
| `GetWindowRect` | `SDL_GetWindowPosition()` + `SDL_GetWindowSize()` | Fill `RECT` from two SDL calls. |
| `GetClientRect` | `SDL_GetWindowSize()` | Fill `RECT` with `(0, 0, w, h)`. |
| `AdjustWindowRect` | Stub (always `TRUE`) | Window frame padding not relevant under SDL2. |
| `AdjustWindowRectEx` | Stub (always `TRUE`) | Same as above. |
| `RegisterClassA` | Stub (always `TRUE`) | WNDCLASS is consumed internally. Store callback pointer in internal table keyed by class name. |
| `GetWindowLongA` / `SetWindowLongA` | Internal hashmap (HWND → LONG) | Store window user data. GWL_WNDPROC used for subclassing — must dispatch to stored proc pointer. No SDL2 equivalent. |
| `IsWindow` | `SDL_HasWindowFlags()` or internal check | Return `TRUE` if handle exists in internal window table. |
| `GetDC` | `SDL_GetWindowSurface()` wrapper | Return a mock `HDC` that internally holds a pointer to `SDL_Surface*`. |
| `ReleaseDC` | No-op (HDC is not reference-counted in SDL) | Return `1`. |
| `BeginPaint` / `EndPaint` | Stub | Return mock `HDC` (same as `GetDC`). |
| `InvalidateRect` / `ValidateRect` | Stub | Return `TRUE`. |
| `UpdateWindow` | Stub | Return `TRUE`. |
| `GetMessageA` | `SDL_WaitEvent()` | Blocking poll. Translate `SDL_Event` → `MSG`. |
| `PeekMessageA` | `SDL_PeepEvents()` with `SDL_GETEVENT` | Non-blocking. Map return: 1=found, 0=empty, -1=error. |
| `DispatchMessageA` | Call stored WNDPROC | Route to `CallWindowProcA` with the window's registered proc. |
| `PostMessageA` | `SDL_PushEvent()` | Translate `MSG` → `SDL_Event`, push to queue. |
| `PostQuitMessage` | `SDL_PushEvent()` with `SDL_QUIT` | Push quit event. |
| `SendMessageA` | Direct WNDPROC call | Synchronous. Call window proc directly. Special case: `WM_GETTEXT`, `WM_SETTEXT` → title, `WM_GETCLIENTAREA` → client rect. |
| `DefWindowProcA` | Stub (return 0) | Default behavior is "no-op" under SDL2. |
| `CallWindowProcA` | Direct function pointer call | Call the stored proc pointer (from `SetWindowLongA`). |
| `SetWindowLongA` | Internal hashmap (HWND → proc/data) | Must handle `GWL_WNDPROC` for subclassing. |
| `GetSystemMetrics` | `SDL_GetCurrentDisplayMode()` or hardcoded | Map `SM_CXSCREEN` → display width, `SM_CYSCREEN` → display height, `SM_CXBORDER` etc. → 0. Most values can be zeroed. |
| `GetAsyncKeyState` | `SDL_GetKeyboardState()` | Map `int` virtual-key → `SDL_Scancode`. Return top bit (pressed) + high bit (toggle state) in `SHORT`. |
| `GetDesktopWindow` | Return sentinel `HWND` | Stub: return a special handle representing "desktop". |
| `GetActiveWindow` | Return current `SDL_Window*` as `HWND` | Stub: return the main game window. |
| `GetFocus` | Return current `SDL_Window*` as `HWND` | Same as above. |
| `SetFocus` | Stub (return previous `HWND`) | No SDL2 focus equivalent needed. |
| `EnableWindow` | Stub (return `TRUE`) | Irrelevant under SDL2. |
| `SetCursorPos` | `SDL_WarpMouseInWindow()` | Map `(x, y)` to window-local coords. |
| `SetCursor` | `SDL_SetCursor()` | Map `HCURSOR` → `SDL_Cursor*` (use SDL default or `SDL_CreateSystemCursor()`). |
| `LoadCursorA` | `SDL_CreateSystemCursor()` | Map `IDC_ARROW`, `IDC_CROSS` etc. → `SDL_SYSTEM_CURSOR_*`. |
| `ClipCursor` | Stub (return `TRUE`) | No SDL2 equivalent. |
| `LoadIconA` | Stub (return sentinel) | Not used by game rendering. |
| `LoadStringA` | Internal string table lookup | Store strings indexed by ID in a map. |
| `SetRect` | Manual struct fill | Trivial: `r.left=x, r.top=y, r.right=x2, r.bottom=y2`. No SDL2 function needed. |
| `CreateDialogParamA` | Internal dialog manager | **No SDL2 equivalent.** Requires custom dialog UI system or stub. Game uses this for options menus. |
| `IsDialogMessageA` | Internal dialog manager | Route to dialog window proc if active. |
| `GetDlgItem` | Internal dialog control table | Lookup control handle from dialog + ID. |
| `CheckDlgButton` / `IsDlgButtonChecked` | Internal dialog state | Store checkbox state per control. |
| `SetDlgItemTextA` | Internal dialog state | Set text label per control. |
| `MessageBoxA` | `SDL_ShowSimpleMessageBox()` | Direct map. |
| `wsprintfA` | `snprintf()` (C stdlib) | Win32-style `vsprintf` wrapper. Translate `%` format specifiers. |
| `SetWindowsHookExA` | Stub (return `NULL`) | Keyboard hook for key-repeat; can be replaced by polling `SDL_GetKeyState()`. |
| `CallNextHookEx` | Stub (return 0) | No-op if hook chain is empty. |
| `UnhookWindowsHookEx` | Stub (return `TRUE`) | No-op. |
| `SystemParametersInfoA` | Stub (return `TRUE`) | Game queries mouse speed / double-click time. |
| `MapWindowPoints` | Stub (identity transform) | No window hierarchy under SDL2. |
| `GetDlgItemTextA` | *(not imported)* | — |

**Key custom implementation needed:**
- **HWND / HDC / HCURSOR / HFONT / HPALETTE type wrappers** — internal opaque structs with integer IDs that map to SDL2 objects. These are passed around through the entire codebase.
- **WNDPROC table** — `RegisterClassA` and `SetWindowLongA` with `GWL_WNDPROC` require storing function pointers per window. `SendMessageA` / `DispatchMessageA` dispatch through this table.
- **Dialog system** — `CreateDialogParamA` and related 6 functions have no SDL2 equivalent. Minimal implementation: parse dialog resource, store control state in memory. No rendering needed if game draws its own UI via DirectDraw/GDI.

---

## gdi32 → SDL2 Surface / Pixel

**16 functions**

| Function | SDL2 Equivalent | Notes / Gap |
|----------|----------------|-------------|
| `CreateDCA` | `SDL_GetWindowSurface()` (via mock HDC) | "Display DC" → wrap the window surface. |
| `DeleteDC` | No-op on mock HDC | Return `TRUE`. |
| `CreateDIBitmap` | `SDL_CreateRGBSurfaceFrom()` or `SDL_CreateRGBSurface()` + `SDL_ConvertPixels()` | Map `BITMAPINFOHEADER` (`biWidth`, `biHeight`, `biBitCount`, `biCompression`) → `SDL_PixelFormat` + surface allocation. Handle BI_RGB only. |
| `CreatePalette` | `SDL_Palette*` + `SDL_SetPaletteColors()` | Map `LOGPALETTE` → `SDL_Color[]`, create `SDL_Palette*`. Associate with HDC. |
| `SelectPalette` | Swap `SDL_Palette*` on HDC | Store active palette per HDC. |
| `RealizePalette` | `SDL_SetPaletteColors()` on surface | Apply palette to any surfaces created under this HDC. |
| `GetSystemPaletteEntries` | `SDL_GetPaletteColors()` | Copy current palette colors to output buffer. |
| `StretchDIBits` | `SDL_BlitSurface()` or `SDL_UpperBlitScaled()` / `SDL_SoftStretch()` | **Critical.** This is the software rendering fallback path. Map source DIB buffer → `SDL_Surface*` → blit to HDC's surface with scaling. `destBlt` = `SRCCOPY` means simple blit. |
| `CreateFontA` | Stub (return sentinel `HFONT`) | Game uses GDI text rendering rarely. Store font metrics (height) for `GetObjectA`. |
| `GetObjectA` | Return stored metrics | For `HBITMAP` → `BITMAP` struct (width, height, bits ptr). For `HFONT` → `LOGFONT` (height). |
| `GetStockObject` | Stub (return sentinel handle) | WHITEBRUSH, BLACKPEN etc. — return valid-but-dummy handle. |
| `DeleteObject` | `SDL_FreeSurface()` / `SDL_FreePalette()` / free sentinel | Check object type tag → free appropriate resource. |
| `GetDeviceCaps` | Hardcoded or `SDL_GetCurrentDisplayMode()` | Map `BITSPIXEL` → 16/32, `DESKTOPHEIGHT/WIDTH` → display size. Most values → 0 or 1. |
| `SetBkColor` / `SetTextColor` | Store color on HDC | Trivial: store `COLORREF` in HDC struct for text rendering. |
| `UnrealizeObject` | Stub (return `TRUE`) | No-op. |

**Key observations:**
- `StretchDIBits` is the most critical GDI function. DOOM95 may use it as a fallback rendering path when DirectDraw is unavailable.
- Palette functions (`CreatePalette`, `SelectPalette`, `RealizePalette`) must coordinate with SDL2's `SDL_Palette` for 8-bit surfaces.
- All GDI handles (`HDC`, `HBITMAP`, `HFONT`, `HPALETTE`) must be allocated with integer IDs and tracked in an internal table.

---

## ddraw → SDL2 Texture / Render

**1 function imported + vtable methods called dynamically**

| Function | SDL2 Equivalent | Notes / Gap |
|----------|----------------|-------------|
| `DirectDrawCreate` | **Custom mock vtable** | Return an `IDirectDraw` interface pointer (struct of function pointers). The game calls all subsequent ddraw methods through this vtable. |

### IDirectDraw vtable methods (must all be stubbed/mocked)

| vtable method | SDL2 Equivalent | Notes |
|---------------|----------------|-------|
| `CreateSurface` | `SDL_CreateRGBSurface()` / `SDL_CreateTexture()` | Map `DDSURFACEDESC` (`dwWidth`, `dwHeight`, `ddPixelFormat`) → SDL surface/texture. Track surface handles. |
| `Flip` | `SDL_UpdateWindowSurface()` or present | Swap front/back buffers. For fullscreen: `SDL_UpdateWindowSurface()` or `SDL_GL_SwapWindow()`. |
| `Blt` | `SDL_BlitSurface()` or `SDL_BlitScaled()` | Rect-to-rect copy. Map `DDBLTFX` (color fill, etc.) to `SDL_FillRect()` or blit. |
| `BltFast` | `SDL_BlitSurface()` | Simplified blit without effects. |
| `GetSurfaceDesc` | Copy surface metadata | Return `DDSURFACEDESC` with `lPitch`, `lpSurface` (pixel data pointer), etc. |
| `Lock` | Access surface pixels directly | Return pointer to `SDL_Surface->pixels`. Store pitch. |
| `Unlock` | No-op (SDL surfaces don't require unlock) | Return `DD_OK`. |
| `Restore` | No-op (no video mode switch in SDL2) | Return `DD_OK` or re-create surface. |
| `SetPalette` | `SDL_SetPaletteColors()` | Map `LPPALETTEENTRY[]` → `SDL_Color[]`. |
| `GetDC` / `ReleaseDC` | `SDL_GetWindowSurface()` / no-op | Return mock `HDC` (shared with GDI path). |
| `AddOverlayDirtyRect` | Stub (return `DD_OK`) | Overlay not used. |
| `SetClipper` / `GetClipper` | Stub | Return `DD_OK`. |
| `SetCooperativeLevel` | Stub | Return `DD_OK`. Game passes `DDSCL_EXCLUSIVE|DDSCL_FULLSCREEN` for fullscreen. |
| `SetDisplayMode` | `SDL_SetWindowFullscreen()` + `SDL_CreateWindow()` with new dims | Map width/height/bpp → `SDL_CreateWindow()` or `SDL_SetWindowFullscreen()`. |
| `GetMonitorFrequency` | Stub | Return some value. |
| `GetFourCCCodes` | Stub | Return NULL. |
| `GetAvailableVidMem` | Stub | Return fake value. |
| `GetCaps` | Fill `DDCAPS` struct | Set `DDCAPS_PRIMARYSURFACE`, `DDCAPS_FLIP`, etc. |

**Critical implementation notes:**
- The entire `IDirectDraw` interface must be a struct of function pointers that DOOM95 calls.
- Surfaces created via `CreateSurface` need their own vtable (`IDirectDrawSurface`) with `Blt`, `Lock`, `Unlock`, `GetDC`, `ReleaseDC`, `AddDirtyRect`, etc.
- `Lock`/`Unlock` must return the raw pixel buffer — DOOM95 writes directly to these buffers.
- Palette changes via `SetPalette` must propagate to all surfaces using that palette.

---

## dsound → SDL2 Audio

**1 function imported + vtable methods called dynamically**

| Function | SDL2 Equivalent | Notes / Gap |
|----------|----------------|-------------|
| `DirectSoundCreate` | **Custom mock vtable** | Return `IDirectSound` interface pointer. Init `SDL_AudioSpec` (22050 Hz, 16-bit, stereo) on first use. |

### IDirectSound vtable methods

| vtable method | SDL2 Equivalent | Notes |
|---------------|----------------|-------|
| `CreateSoundBuffer` | `SDL_AudioStream*` or buffer in memory pool | Map `WAVEFORMATEX` → SDL audio format. Allocate buffer. |
| `GetCaps` | Fill `DSBCAPS` struct | Report max buffers, etc. |
| `GetCursorPos` | Stub | Return 0. |
| `Initialize` | No-op (done in `DirectSoundCreate`) | Return `DS_OK`. |
| `Play` | `SDL_QueueAudio()` or mark for mixing | For primary buffer: no-op. For secondary: queue/mix into output buffer. |
| `Stop` | `SDL_ClearQueuedAudio()` or unflag buffer | Stop mixing this buffer. |
| `SetVolume` | Scale buffer amplitude in mixer | Map `L` (-10000=mute to 0=full) → gain multiplier. |
| `SetFrequency` | Resample or flag for resampling | Map `dwFrequency` → SDL resampling. Complex — consider stubbing with fixed 22050. |
| `SetPan` | Stereo panning in mixer | Scale left/right channels. |
| `Lock` / `Unlock` | Return pointer to buffer memory | Similar to DirectDraw `Lock`. Game writes PCM data here. |
| `SetFormat` | Store `WAVEFORMATEX` | Update buffer format. |
| `SetStatus` | Set PLAYING/STOPPED flag | DSBCSTATUS_PLAYING → start mixing. |
| `Restore` | No-op | Return `DS_OK`. |

### Critical implementation notes:
- DOOM95 creates ~16-32 sound buffers. Each buffer is a `IDirectSoundBuffer` with its own vtable.
- The **audio mixer** must be custom: each frame, iterate all PLAYING buffers, read their locked PCM data, apply volume/pan/frequency, mix into a single output buffer, then `SDL_QueueAudio()`.
- `SDL_OpenAudioDevice()` is called once at init with a 22050 Hz 16-bit stereo `SDL_AudioSpec`.
- MIDI is NOT part of DirectSound — it's in WINMM (see below).

---

## winmm → SDL2 Timer + Joystick + MIDI

**15 functions**

### Timer (1 function)

| Function | SDL2 Equivalent | Notes |
|----------|----------------|-------|
| `timeGetTime` | `SDL_GetTicks()` | Direct replacement. Both return `DWORD` / `Uint32` milliseconds. |

### Joystick (4 functions)

| Function | SDL2 Equivalent | Notes |
|----------|----------------|-------|
| `joyGetNumDevs` | `SDL_NumJoysticks()` | Direct map. Both return count. |
| `joyGetDevCapsA` | `SDL_JoystickName()` + `SDL_JoystickNumAxes()` / `SDL_JoystickNumButtons()` | Open joystick to query caps, fill `JOYCAPSA` (axes count, buttons, min/max). Then close. |
| `joyGetPosEx` | `SDL_JoystickGetAxis()` + `SDL_JoystickGetButton()` | Read axis values (0-65535, mapped from SDL's -32768/+32767) and button states. Fill `JOYINFOEX`. |
| `joyGetPos` | *(not imported — game uses Ex)* | — |

**Implementation approach:**
- Maintain an array of `SDL_Joystick*` pointers, opened on first `joyGetPosEx` call.
- `UWORD` range 0–65535 maps from `Sint16` range -32768–32767 via `(axis + 32768) * 2 - 1`.

### MIDI (10 functions)

| Function | SDL2 Equivalent | Notes / Gap |
|----------|----------------|-------------|
| `midiStreamOpen` | **No SDL2 equivalent** | **Custom implementation needed.** MIDI playback requires a synthesizer library (Timidity++, FluidSynth) or SDL_mixer with MIDI support. |
| `midiStreamOut` | Feed MIDI events to synth | Pipe MIDI bytes to the chosen synth library. |
| `midiStreamClose` | Close synth, free resources | Direct cleanup. |
| `midiStreamPause` | Pause synth playback | No SDL2 equivalent. Custom synth wrapper. |
| `midiStreamRestart` | Resume synth | Custom. |
| `midiStreamProperty` | Stub (return `MMSYSERR_NOERROR`) | Unused. |
| `midiOutGetNumDevs` | Stub (return 0 or 1) | Game uses for detection; return 1 if MIDI synth available. |
| `midiOutPrepareHeader` | No-op (return `MMSYSERR_NOERROR`) | MIDI header management is WinMM-specific. |
| `midiOutUnprepareHeader` | No-op (return `MMSYSERR_NOERROR`) | Same. |
| `midiOutReset` | No-op | Return `MMSYSERR_NOERROR`. |
| `midiOutSetVolume` | Stub | Adjust synth volume if available. |

**Gap: MIDI has no direct SDL2 support.** Options:
1. **Stub everything** — game runs silent for music. DOOM95 falls back to WAV sound effects.
2. **Link libmodplug or Timidity++** — full MIDI playback.
3. **SDL_mixer** — supports MIDI via native drivers (Timidity).

---

## advapi32 → In-memory registry (no SDL2 equivalent)

**5 functions** — All require a custom in-memory registry.

| Function | SDL2 Equivalent | Notes |
|----------|----------------|-------|
| `RegCreateKeyA` | In-memory key creation | Create a node in a flat hashmap (path → key node). Return a mock `HKEY`. |
| `RegOpenKeyA` | Lookup key in hashmap | Return mock `HKEY` or `ERROR_FILE_NOT_FOUND`. |
| `RegCloseKey` | Decrement refcount on mock `HKEY` | Return `ERROR_SUCCESS`. |
| `RegQueryValueExA` | Lookup value under key | Return `ERROR_SUCCESS` + value data. If not set, return `ERROR_FILE_NOT_FOUND`. |
| `RegSetValueExA` | Store value under key | Write to hashmap. Return `ERROR_SUCCESS`. |

**Implementation approach:**
- A single global `struct registry { hashmap<string, node> keys; }` where each node maps value name → `{ type, data, size }`.
- `HKEY` values are just `uintptr_t` pointers to nodes or sentinel values like `HKEY_CURRENT_USER`.
- DOOM95 reads: install path, last resolution, saved options. Writes: settings changes.

---

## kernel32 → SDL2 + C stdlib + custom

**69 unique functions** (20 from `kernel32.dll` + 49 from `kernel32.DLL`, with 10 duplicates)

### Memory allocation (3 functions)

| Function | SDL2 Equivalent | Notes |
|----------|----------------|-------|
| `GlobalAlloc` | `malloc()` | `GMEM_MOVEABLE` and `GMEM_FIXED` — SDL doesn't care. Just `malloc(size)`. |
| `LocalAlloc` | `malloc()` | Same. `LMEM_MOVEABLE` / `LMEM_FIXED` ignored. |
| `LocalFree` | `free()` | Trivial. |

### File I/O (12 unique functions)

| Function | SDL2 Equivalent | Notes |
|----------|----------------|-------|
| `CreateFileA` | `fopen()` or `SDL_RWFromFile()` | `dwDesiredAccess` GENERIC_READ → "rb", GENERIC_WRITE → "wb". Return `HANDLE` wrapping `FILE*`. |
| `ReadFile` | `fread()` | Read from `FILE*` wrapped by `HANDLE`. |
| `WriteFile` | `fwrite()` | Same. |
| `SetFilePointer` | `fseek()` | `dwMoveMethod` (FILE_BEGIN/SET/END) maps to `SEEK_SET/END/CUR`. |
| `GetFileSize` | `fseek()` + `ftell()` | Or `stat()`. |
| `CloseHandle` (file) | `fclose()` | Check if HANDLE is a file handle → `fclose()`. Otherwise delegate to event/mutex handle. |
| `GetFileAttributesA` | `stat()` | Check `S_ISREG`, `S_ISDIR`. Return `FILE_ATTRIBUTE_*` bits. |
| `FindNextFileA` | `readdir()` | Wrap `DIR*` in `HANDLE`. Fill `WIN32_FIND_DATAA`. |
| `DeleteFileA` | `remove()` | Trivial. |
| `CreateDirectoryA` | `mkdir()` | Trivial. |
| `GetModuleFileNameA` | Store path at init | Return the path of DOOM95.EXE (resolved at load time). |

### Threading / Synchronization (8 functions)

| Function | SDL2 Equivalent | Notes |
|----------|----------------|-------|
| `CreateThread` | `SDL_CreateThread()` | Map thread entry point + param → SDL thread function. Return `HANDLE` wrapping `SDL_Thread*`. |
| `ExitThread` | `SDL_ExitThread()` | Trivial. |
| `GetCurrentThreadId` | `SDL_ThreadID()` | Direct replacement. Returns `DWORD` / `Uint32`. |
| `CreateEventA` | `SDL_CreateSemaphore()` | Auto-reset event → `SDL_Sem` with initial value 0. Manual-reset → flag stored alongside sem. |
| `SetEvent` | `SDL_SemPost()` | Signal the semaphore. |
| `WaitForSingleObject` | `SDL_SemWait()` or `SDL_SemWaitTimeout()` | `INFINITE` → `SDL_SemWait()`. Timeout → `SDL_SemWaitTimeout()`. |
| `CreateMutexA` | `SDL_CreateMutex()` | Map to `SDL_mutex*`. |
| `ReleaseMutex` | `SDL_UnlockMutex()` | Trivial. |

### Timing (2 functions)

| Function | SDL2 Equivalent | Notes |
|----------|----------------|-------|
| `GetTickCount` | `SDL_GetTicks()` | Direct replacement. Both return milliseconds since boot/SDL init. |
| `Sleep` | `SDL_Delay()` | Direct replacement. Both take milliseconds. |

### Thread-local storage (4 functions)

| Function | SDL2 Equivalent | Notes |
|----------|----------------|-------|
| `TlsAlloc` | Allocate index from pool | `DWORD` index → index into global `void**` array. Return next free index. |
| `TlsSetValue` | `tls_array[index][thread_id] = value` | Per-thread per-slot storage. Use a 2D array or hashmap. |
| `TlsGetValue` | `tls_array[index][thread_id]` | Lookup. |
| `TlsFree` | Free slot for current thread | Set `tls_array[index][thread_id] = NULL`. |

**Gap:** SDL2 has no Tls API. Must implement a per-thread key-value store using `SDL_ThreadID()` as key.

### System info (5 functions)

| Function | SDL2 Equivalent | Notes |
|----------|----------------|-------|
| `GetSystemInfo` | `SDL_CPUCount()` + hardcoded | Fill `SYSTEM_INFO` struct: `dwNumberOfProcessors = SDL_CPUCount()`, others hardcoded (x86). |
| `GetVersion` | Return `0x80000005` | Hardcoded "Windows 2000". Game uses this for feature detection. |
| `GetCommandLineA` | `argv` reconstructed | Reconstruct command line string from `argv` at init. |
| `GetEnvironmentStrings` | `environ` or stub | Return NULL or pointer to environment block. |
| `GetTimeZoneInformation` | `localtime()` or stub | Return `TIME_ZONE_ID_UNKNOWN` with zeroed bias. |

### DLL loading (4 functions)

| Function | SDL2 Equivalent | Notes |
|----------|----------------|-------|
| `LoadLibraryA` | Internal DLL resolver | Look up DLL name in my_wine's registered DLL table. Return `HMODULE` = `uintptr_t` to DLL struct. |
| `GetProcAddress` | Lookup in DLL export table | Find function by name in the DLL's export map. Return function pointer (trampoline). |
| `FreeLibrary` | Decrement DLL refcount | Don't actually unload. |
| `GetModuleHandleA` | Lookup by name in DLL table | Return `HMODULE` for "my_wine" or DOOM95.EXE if name is NULL. |

### Resources (6 functions)

| Function | SDL2 Equivalent | Notes |
|----------|----------------|-------|
| `FindResourceA` | Binary resource table lookup | Search PE resource directory for `(type, name)`. Return pointer to resource data + size. |
| `LoadResource` | Return raw pointer to resource data | No-op (data is in the PE image). Return `HGLOBAL` pointing to resource bytes. |
| `LockResource` | Return raw pointer | Same pointer as `LoadResource`. |
| `SizeofResource` | Return resource size | Stored in PE resource directory. |

### Error handling (1 function)

| Function | SDL2 Equivalent | Notes |
|----------|----------------|-------|
| `GetLastError` | `errno` or internal thread-local | Store last error in thread-local storage. Set by each API stub on failure. |

### Process (2 functions)

| Function | SDL2 Equivalent | Notes |
|----------|----------------|-------|
| `ExitProcess` | `exit()` or longjmp to host | Clean up SDL2 subsystems before exit. |

### Miscellaneous stubs (12 functions)

| Function | SDL2 Equivalent | Notes |
|----------|----------------|-------|
| `DeviceIoControl` | Stub (return `FALSE`) | Joystick I/O. Can wire to SDL joystick if needed. |
| `SearchPathA` | `strchr()` / path concat | Resolve relative paths. Stub: copy filename to output. |
| `GetStdHandle` | Return sentinel `HANDLE` | Return special HANDLE for stdin/stdout/stderr. |
| `SetStdHandle` | Stub (return `TRUE`) | No-op. |
| `WriteConsoleA` | `printf()` or stub | Map to `printf()` for debug output. |
| `ReadConsoleInputA` | Stub (return `FALSE`) | Never called in game mode. |
| `GetConsoleMode` | Stub (return `FALSE`) | — |
| `SetConsoleMode` | Stub (return `FALSE`) | — |
| `GetCPInfo` | Stub (return `TRUE`) | Fill with CP-1252 info. |
| `DosDateTimeToFileTime` | Manual conversion | DOS date/time → `FILETIME`. Calendar math. |
| `FileTimeToDosDateTime` | Reverse conversion | `FILETIME` → DOS date/time. |
| `FileTimeToLocalFileTime` | Stub (identity copy) | Timezone offset = 0. |
| `LocalFileTimeToFileTime` | Stub (identity copy) | Same. |
| `GetFileTime` | `stat()` or stub | Fill with current time or file's mtime. |
| `GetFileType` | Stub (return `FILE_TYPE_DISK`) | — |
| `GetCurrentProcessId` | `getpid()` | C stdlib `<unistd.h>`. |
| `GetCurrentThread` | Return pseudo-handle | `HANDLE` = `(void*)-2` (Windows constant). |
| `GetModuleFileNameA` | Stored path | See above. |

---

## dplay → Custom (multiplayer stub)

**1 ordinal import**

| Function | SDL2 Equivalent | Notes |
|----------|----------------|-------|
| `Ordinal #1` | Stub (return `DP_OK`) | **Custom implementation needed.** DirectPlay init. Likely `DPCreate` or factory function. Stub returns a mock interface with no-op methods. Multiplayer not required for single-player. |

---

## Unmapped Functions — No SDL2 Equivalent

These functions require **custom implementation** with no direct SDL2 or C standard library mapping:

| # | Function | DLL | Required Implementation |
|---|----------|-----|------------------------|
| 1 | `CreateDialogParamA` + 6 dialog helpers | user32 | **Custom dialog UI system** — parse dialog resource, render controls via GDI/DDraw, route messages. |
| 2 | `WNDPROC` table (`RegisterClassA`, `SetWindowLongA`, `SendMessageA`, `DispatchMessageA`) | user32 | **Message dispatch system** — store WNDPROC per window, route messages synchronously and asynchronously. |
| 3 | `DirectDrawCreate` + full vtable (~18 methods) | ddraw | **Complete DirectDraw mock** — surface pool, `Lock`/`Unlock` for raw pixel access, `Blt`/`BltFast` for pixel copy, `Flip` for presentation. |
| 4 | `DirectSoundCreate` + full vtable (~12 methods) | dsound | **Complete DirectSound mock** — buffer pool, PCM mixer, volume/pan/frequency per buffer, `Lock`/`Unlock` for sample writing. |
| 5 | `midiStreamOpen` + 7 MIDI functions | winmm | **MIDI playback system** — link libmodplug/Timidity++/SDL_mixer, or stub for silent music. |
| 6 | `RegCreateKeyA` + 4 registry functions | advapi32 | **In-memory registry** — flat hashmap with key path, value name → (type, data, size). |
| 7 | `TlsAlloc` + 3 TLS functions | kernel32 | **Per-thread key-value store** — 2D array indexed by `[slot][SDL_ThreadID()]`. |
| 8 | `Ordinal #1` (DirectPlay) | dplay | **Multiplayer stub** — mock `IDirectPlay` interface, return `DP_OK` from all methods. |
| 9 | PE resource system (`FindResourceA`, `LoadResource`, `LockResource`, `SizeofResource`) | kernel32 | **PE resource directory parser** — parse the game's embedded resources (icons, cursors, strings, dialogs) from the PE image. |
| 10 | Handle management (`HANDLE`, `HWND`, `HDC`, `HBITMAP`, `HFONT`, `HPALETTE`, `HRSRC`, `HGLOBAL`, `HLOCAL`, `HHOOK`, `HMODULE`, `HMIDIOUT`, `HMIDISTREAM`) | all | **Handle allocator** — opaque `uintptr_t` IDs that map to internal structs (SDL objects, memory, metadata). |

---

## Implementation Complexity Summary

| Category | Functions | SDL2 Coverage | Custom Work |
|----------|-----------|---------------|-------------|
| user32 (window/events) | 56 | ~40 mapped to SDL2 | WNDPROC table, dialog system, handle management |
| gdi32 (surface/GDI) | 16 | ~12 mapped to SDL2 | Handle management, palette sync, `StretchDIBits` |
| ddraw (rendering) | 1 + vtable | 0 (mock vtable wraps SDL2) | Full vtable (~18 methods), surface pool, `Lock`/`Unlock` |
| dsound (audio) | 1 + vtable | 0 (mock vtable wraps SDL2) | Full vtable (~12 methods), PCM mixer, `Lock`/`Unlock` |
| winmm (timer/joystick/MIDI) | 15 | 5 (timer + joystick) | MIDI subsystem (stub or libmodplug) |
| kernel32 (system) | 69 | ~30 mapped to SDL2/C stdlib | TLS, registry (via advapi32), PE resources, handle types |
| advapi32 (registry) | 5 | 0 | In-memory key-value store |
| dplay (multiplayer) | 1 | 0 | Mock interface (single-player only) |

### Bottom Line

- **~85 functions** can be directly mapped to SDL2 or C standard library
- **~30 functions** need thin wrappers / simple stubs
- **~50 functions** (across ddraw vtable, dsound vtable, MIDI, TLS, registry, dialog, message dispatch) need custom implementation
- **Total stubs/implementation targets: ~165 entry points** (including vtable methods)
