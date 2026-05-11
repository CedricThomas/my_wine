# File Layout

## Source Tree

```
my_wine/
├── include/
│   ├── user32_types.h          # HWND, HDC, HCURSOR, MSG, RECT, WNDCLASSA, PAINTSTRUCT
│   ├── gdi32_types.h           # HBITMAP, HPALETTE, HFONT, LOGPALETTE, BITMAPINFO, COLORREF
│   ├── ddraw_types.h           # DDSURFACEDESC, DDCAPS, DDSCAPS, DDPIXELFORMAT, HRESULT codes
│   ├── dsound_types.h          # WAVEFORMATEX, DSBCAPS, DSCAPS, DSBDIRECTION, HRESULT codes
│   ├── winmm_types.h           # JOYCAPSA, JOYINFOEX, MIDIHDR, MMRESULT codes
│   ├── advapi32_types.h        # HKEY, LSTATUS, REG_VALUE types
│   ├── dialog_types.h          # DIALOGTEMPLATE, control types, dialog state
│   └── render_backend.h        # (copy from src/backend/)
│
├── src/stubs/
│   ├── user32_window.c         # CreateWindowExA, DestroyWindow, ShowWindow, SetWindowPos,
│   │                          # MoveWindow, SetWindowTextA, GetWindowRect, GetClientRect,
│   │                          # AdjustWindowRect, AdjustWindowRectEx, RegisterClassA,
│   │                          # GetWindowLongA, SetWindowLongA, IsWindow, EnableWindow,
│   │                          # GetDesktopWindow, GetActiveWindow, GetFocus, SetFocus,
│   │                          # UpdateWindow, InvalidateRect, ValidateRect,
│   │                          # BeginPaint, EndPaint, MapWindowPoints, GetSystemMetrics
│   ├── user32_message.c        # GetMessageA, PeekMessageA, DispatchMessageA,
│   │                          # PostMessageA, PostQuitMessage, SendMessageA,
│   │                          # DefWindowProcA, CallWindowProcA, SetWindowsHookExA,
│   │                          # UnhookWindowsHookEx, CallNextHookEx, SystemParametersInfoA
│   ├── user32_dialog.c         # CreateDialogParamA, IsDialogMessageA, GetDlgItem,
│   │                          # CheckDlgButton, IsDlgButtonChecked, SetDlgItemTextA,
│   │                          # MessageBoxA, LoadStringA
│   ├── user32_input.c          # GetAsyncKeyState, LoadCursorA, SetCursor, SetCursorPos,
│   │                          # ClipCursor, LoadIconA, wsprintfA, SetRect
│   ├── gdi32_surface.c         # CreateDCA, GetDC, ReleaseDC, DeleteDC,
│   │                          # CreateDIBitmap, StretchDIBits, GetObjectA,
│   │                          # DeleteObject, GetDeviceCaps, GetStockObject
│   ├── gdi32_palette.c         # CreatePalette, SelectPalette, RealizePalette,
│   │                          # GetSystemPaletteEntries, SetBkColor, SetTextColor,
│   │                          # UnrealizeObject
│   ├── gdi32_font.c            # CreateFontA (stub: store metrics)
│   ├── ddraw_interface.c       # DirectDrawCreate + IDirectDraw vtable (18 methods):
│   │                          # SetCooperativeLevel, SetDisplayMode, CreateSurface,
│   │                          # CreatePalette, CreateClipper, GetAvailableVidMem,
│   │                          # GetMonitorFrequency, GetFourCCCodes, GetCaps,
│   │                          # RestoreDisplayMode, GetDisplayMode, GetDC, ReleaseDC,
│   │                          # Compact, GetFlipStatus, WaitForVerticalBlank,
│   │                          # GetGDIEvent, GetSurfaceFromDC, EnumDisplayModes
│   ├── ddraw_surface.c         # IDirectDrawSurface vtable (24 methods):
│   │                          # Lock, Unlock, Blt, BltFast, GetSurfaceDesc,
│   │                          # SetPalette, GetDC, ReleaseDC, Restore, IsLost,
│   │                          # AddAttachedSurface, DeleteAttachedSurface, GetAttachedSurface,
│   │                          # AddOverlayDirtyRect, GetBltStatus, GetFlipStatus,
│   │                          # SetOverlayPosition, GetOverlayPosition,
│   │                          # SetAttachedSurface, SetClipper, GetClipper,
│   │                          # OverrideCursor, GetOverrideCursor, BltBatch
│   ├── dsound_interface.c      # DirectSoundCreate + IDirectSound vtable (12 methods):
│   │                          # CreateSoundBuffer, GetCaps, GetCooperativeLevel,
│   │                          # SetCooperativeLevel, Initialize, Compact,
│   │                          # DuplicateSoundBuffer, GetSpeakerConfig, SetSpeakerConfig,
│   │                          # OpenDevice, CloseDevice, EnumerateDevCaps
│   ├── dsound_buffer.c         # IDirectSoundBuffer vtable (12 methods) + PCM mixer:
│   │                          # Lock, Unlock, Play, Stop, SetVolume, SetPan,
│   │                          # SetFrequency, SetFormat, SetStatus, GetCaps,
│   │                          # GetFormat, GetVolume, Restore, GetCursorPos,
│   │                          # SetLoopPoints + rb_audio_* → mixer loop
│   ├── winmm_timer.c           # timeGetTime
│   ├── winmm_joystick.c        # joyGetNumDevs, joyGetDevCapsA, joyGetPosEx
│   ├── winmm_midi.c            # midiStreamOpen, midiStreamOut, midiStreamClose,
│   │                          # midiStreamPause, midiStreamRestart, midiStreamProperty,
│   │                          # midiOutGetNumDevs, midiOutPrepareHeader,
│   │                          # midiOutUnprepareHeader, midiOutReset, midiOutSetVolume
│   ├── advapi32_registry.c     # RegCreateKeyA, RegOpenKeyA, RegCloseKey,
│   │                          # RegQueryValueExA, RegSetValueExA
│   ├── kernel32_extended.c     # CreateFileA, CloseHandle (extend existing),
│   │                          # SetFilePointer, GetFileSize, GetFileAttributesA,
│   │                          # FindNextFileA, GetModuleFileNameA,
│   │                          # CreateThread, ExitThread, GetCurrentThreadId,
│   │                          # GlobalAlloc, LocalAlloc, LocalFree,
│   │                          # TlsAlloc, TlsFree, TlsSetValue (TlsGetValue exists),
│   │                          # GetTickCount, GetVersion, GetSystemInfo,
│   │                          # GetEnvironmentStrings, GetTimeZoneInformation,
│   │                          # DosDateTimeToFileTime, FileTimeToDosDateTime,
│   │                          # FileTimeToLocalFileTime, LocalFileTimeToFileTime,
│   │                          # GetFileTime, SearchPathA, DeviceIoControl,
│   │                          # GetCPInfo, GetStdHandle, SetStdHandle,
│   │                          # WriteConsoleA, ReadConsoleInputA,
│   │                          # GetConsoleMode, SetConsoleMode,
│   │                          # GetCurrentProcessId, GetCurrentThread,
│   │                          # GetFileType, CreateDirectoryA, DeleteFileA,
│   │                          # FindResourceA, LoadResource, LockResource, SizeofResource
│   └── dplay_stub.c            # DPCreate (ordinal #1) — mock IDirectPlay
│
├── src/backend/
│   └── render_backend_sdl2.c   # SDL2 implementation of render_backend.h interface
│                              # ~2,000 lines covering all rb_* functions
│
└── src/loader/
    ├── import_table.c          # ADD: all new stub entries
    └── ordinal_table.c         # ADD: DPLAY ordinal #1 → DPCreate
```

## File Size Estimates

| File | Approx. Lines | Notes |
|------|---------------|-------|
| `user32_window.c` | 350 | Window lifecycle + WNDPROC table + handle management |
| `user32_message.c` | 300 | Event translation + message queue + dispatch |
| `user32_dialog.c` | 250 | Dialog resource parser + control state + 7 functions |
| `user32_input.c` | 100 | Key state + cursor + string formatting |
| `gdi32_surface.c` | 200 | DC + bitmap + StretchDIBits + object management |
| `gdi32_palette.c` | 120 | Palette create/select/realize + color operations |
| `gdi32_font.c` | 40 | Stub font with metrics storage |
| `ddraw_interface.c` | 350 | IDirectDraw vtable (18 methods) |
| `ddraw_surface.c` | 500 | IDirectDrawSurface vtable (24 methods) + Lock/Unlock/Blt |
| `dsound_interface.c` | 200 | IDirectSound vtable (12 methods) |
| `dsound_buffer.c` | 400 | IDirectSoundBuffer vtable (12 methods) + PCM mixer (~100 lines) |
| `winmm_timer.c` | 10 | Single function |
| `winmm_joystick.c` | 100 | Joystick open/query/state |
| `winmm_midi.c` | 150 | MIDI stubs (or libmodplug wrapper) |
| `advapi32_registry.c` | 120 | In-memory registry hashmap |
| `kernel32_extended.c` | 500 | ~40 new kernel32 functions + resource loader |
| `dplay_stub.c` | 50 | Mock IDirectPlay |
| `render_backend_sdl2.c` | 2,000 | Full SDL2 backend implementation |
| **Headers (10 files)** | ~500 | Type definitions, HRESULT codes, struct layouts |
| **TOTAL** | **~6,000** | New code lines |
