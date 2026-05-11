# DOOM95 Import Analysis

Complete import table extracted from DOOM95.EXE via `objdump -p`.
The binary contains **two separate import tables** (KERNEL32.dll and USER32.dll each appear twice — once lowercase, once uppercase), totaling **164 imported function entries across 9 DLL entries (8 unique DLLs)**.

## DLL: KERNEL32.dll (20 functions) — *first import table entry*

| # | Function | Signature (stdcall, Win32) | Category | Notes |
|---|----------|---------------------------|----------|-------|
| 1 | `CloseHandle` | BOOL CloseHandle(HANDLE hObject) | critical | Close file/object handles |
| 2 | `CreateFileA` | HANDLE CreateFileA(LPCSTR, DWORD, DWORD, LPSECURITY_ATTRIBUTES, DWORD, DWORD, HANDLE) | critical | Open/read game files (WAD, config) |
| 3 | `DeviceIoControl` | BOOL DeviceIoControl(HANDLE, DWORD, LPVOID, DWORD, LPVOID, DWORD, LPDWORD, LPOVERLAPPED) | optional | Input device I/O (joystick passthrough); stub FALSE |
| 4 | `FindResourceA` | HRSRC FindResourceA(HMODULE, LPCSTR, LPCSTR) | critical | Load embedded resources (icons, cursors) |
| 5 | `FreeLibrary` | BOOL FreeLibrary(HMODULE) | critical | Unload DLLs |
| 6 | `GetCurrentThreadId` | DWORD GetCurrentThreadId(void) | critical | Thread identification |
| 7 | `GetLastError` | DWORD GetLastError(void) | critical | Error reporting |
| 8 | `GetProcAddress` | FARPROC GetProcAddress(HMODULE, LPCSTR) | critical | Dynamic function resolution |
| 9 | `GetSystemInfo` | void GetSystemInfo(LPSYSTEM_INFO) | critical | CPU/core count detection |
| 10 | `GetTickCount` | DWORD GetTickCount(void) | critical | Game timing, frame pacing |
| 11 | `GlobalAlloc` | HGLOBAL GlobalAlloc(UINT, SIZE_T) | critical | Legacy memory allocation |
| 12 | `LoadLibraryA` | HMODULE LoadLibraryA(LPCSTR) | critical | Load DLLs at runtime |
| 13 | `LoadResource` | HGLOBAL LoadResource(HMODULE, HRSRC) | critical | Load embedded resources |
| 14 | `LocalAlloc` | HLOCAL LocalAlloc(UINT, UINT) | critical | Legacy memory allocation |
| 15 | `LocalFree` | HLOCAL LocalFree(HLOCAL) | critical | Free local memory |
| 16 | `LockResource` | LPVOID LockResource(HGLOBAL) | critical | Access resource data |
| 17 | `SearchPathA` | DWORD SearchPathA(LPCTSTR, LPCTSTR, LPCTSTR, DWORD, LPTSTR, LPTSTR*) | optional | Path resolution for game files; can provide stub |
| 18 | `SizeofResource` | DWORD SizeofResource(HMODULE, HRSRC) | critical | Resource size queries |
| 19 | `Sleep` | void Sleep(DWORD) | critical | Thread sleep / timing |
| 20 | `WriteFile` | BOOL WriteFile(HANDLE, LPCVOID, DWORD, LPDWORD, LPOVERLAPPED) | critical | Write files (config, save) |

## DLL: ADVAPI32.dll (5 functions)

| # | Function | Signature (stdcall, Win32) | Category | Notes |
|---|----------|---------------------------|----------|-------|
| 21 | `RegCloseKey` | LSTATUS RegCloseKey(HKEY) | cosmetic | Close registry key; can stub |
| 22 | `RegCreateKeyA` | LSTATUS RegCreateKeyA(HKEY, LPCSTR, PHKEY) | cosmetic | Create registry entry; stub ERROR_SUCCESS |
| 23 | `RegOpenKeyA` | LSTATUS RegOpenKeyA(HKEY, LPCSTR, PHKEY) | cosmetic | Open registry key; can stub |
| 24 | `RegQueryValueExA` | LSTATUS RegQueryValueExA(HKEY, LPCSTR, LPDWORD, LPDWORD, LPBYTE, LPDWORD) | cosmetic | Read registry value; stub ERROR_FILE_NOT_FOUND |
| 25 | `RegSetValueExA` | LSTATUS RegSetValueExA(HKEY, LPCSTR, DWORD, DWORD, const BYTE*, DWORD) | cosmetic | Write registry value; stub ERROR_SUCCESS |

## DLL: WINMM.dll (15 functions)

| # | Function | Signature (stdcall, Win32) | Category | Notes |
|---|----------|---------------------------|----------|-------|
| 26 | `joyGetDevCapsA` | MMRESULT joyGetDevCapsA(UINT, LPJOYCAPSA, UINT) | critical | Query joystick capabilities |
| 27 | `joyGetNumDevs` | UINT joyGetNumDevs(void) | critical | Count joysticks |
| 28 | `joyGetPosEx` | MMRESULT joyGetPosEx(UINT, LPJOYINFOEX) | critical | Read joystick state (axes, buttons) |
| 29 | `midiOutGetNumDevs` | UINT midiOutGetNumDevs(void) | critical | Count MIDI output devices |
| 30 | `midiOutPrepareHeader` | MMRESULT midiOutPrepareHeader(HMIDIOUT, LPMIDIHDR, UINT) | critical | Prepare MIDI header for playback |
| 31 | `midiOutReset` | MMRESULT midiOutReset(HMIDIOUT) | optional | Reset MIDI output; can stub |
| 32 | `midiOutSetVolume` | MMRESULT midiOutSetVolume(HMIDIOUT, DWORD) | cosmetic | MIDI volume control; can stub |
| 33 | `midiOutUnprepareHeader` | MMRESULT midiOutUnprepareHeader(HMIDIOUT, LPMIDIHDR, UINT) | critical | Unprepare MIDI header |
| 34 | `midiStreamClose` | MMRESULT midiStreamClose(HMIDISTREAM) | critical | Close MIDI stream |
| 35 | `midiStreamOpen` | MMRESULT midiStreamOpen(LPHMIDISTREAM, LPUINT, UINT, DWORD, DWORD, DWORD) | critical | Open MIDI stream |
| 36 | `midiStreamOut` | MMRESULT midiStreamOut(HMIDISTREAM, LPBYTE, DWORD) | critical | Send MIDI data to stream |
| 37 | `midiStreamPause` | MMRESULT midiStreamPause(HMIDISTREAM) | cosmetic | Pause MIDI playback; can stub |
| 38 | `midiStreamProperty` | MMRESULT midiStreamProperty(HMIDISTREAM, LPBYTE, DWORD) | optional | MIDI stream properties; can stub |
| 39 | `midiStreamRestart` | MMRESULT midiStreamRestart(HMIDISTREAM) | cosmetic | Restart paused MIDI; can stub |
| 40 | `timeGetTime` | DWORD timeGetTime(void) | critical | High-resolution timer |

## DLL: GDI32.dll (16 functions)

| # | Function | Signature (stdcall, Win32) | Category | Notes |
|---|----------|---------------------------|----------|-------|
| 41 | `CreateDCA` | HDC CreateDCA(LPCSTR, LPCSTR, LPCSTR, const DEVMODEA*) | critical | Create device context for display |
| 42 | `CreateDIBitmap` | HBITMAP CreateDIBitmap(HDC, CONST BITMAPINFOHEADER*, DWORD, CONST VOID*, CONST BITMAPINFO*, UINT) | critical | Create bitmap from DIB data |
| 43 | `CreateFontA` | HFONT CreateFontA(int, int, int, int, int, DWORD, DWORD, DWORD, DWORD, DWORD, DWORD, DWORD, DWORD, LPCSTR) | critical | Create font for text rendering |
| 44 | `CreatePalette` | HPALETTE CreatePalette(LOGPALETTE*) | critical | Create color palette (16/256-color) |
| 45 | `DeleteDC` | HDC DeleteDC(HDC) | critical | Release device context |
| 46 | `DeleteObject` | BOOL DeleteObject(HGDIOBJ) | critical | Free GDI objects |
| 47 | `GetDeviceCaps` | int GetDeviceCaps(HDC, int) | critical | Query display caps (bpp, resolution) |
| 48 | `GetObjectA` | int GetObjectA(HGDIOBJ, int, LPVOID) | critical | Get object properties (font, bitmap) |
| 49 | `GetStockObject` | HGDIOBJ GetStockObject(int) | critical | Get stock pen/brush objects |
| 50 | `GetSystemPaletteEntries` | UINT GetSystemPaletteEntries(HDC, UINT, UINT, LPPALETTEENTRY) | critical | Read system palette |
| 51 | `RealizePalette` | UINT RealizePalette(HDC) | critical | Realize palette into display |
| 52 | `SelectPalette` | HPALETTE SelectPalette(HDC, HPALETTE, BOOL) | critical | Activate palette |
| 53 | `SetBkColor` | COLORREF SetBkColor(HDC, COLORREF) | cosmetic | Text background color |
| 54 | `SetTextColor` | COLORREF SetTextColor(HDC, COLORREF) | cosmetic | Text color |
| 55 | `StretchDIBits` | int StretchDIBits(HDC, int, int, int, int, int, int, int, int, CONST VOID*, CONST BITMAPINFO*, UINT, DWORD) | critical | Render DIB to screen (software fallback) |
| 56 | `UnrealizeObject` | BOOL UnrealizeObject(HGDIOBJ) | cosmetic | Remove from system; can stub |

## DLL: USER32.dll (54 functions) — *first import table entry (lowercase)*

| # | Function | Signature (stdcall, Win32) | Category | Notes |
|---|----------|---------------------------|----------|-------|
| 57 | `AdjustWindowRect` | BOOL AdjustWindowRect(LPRECT, DWORD, BOOL) | critical | Calculate window size for client area |
| 58 | `AdjustWindowRectEx` | BOOL AdjustWindowRectEx(LPRECT, DWORD, BOOL, DWORD) | critical | Extended version |
| 59 | `BeginPaint` | HDC BeginPaint(HWND, LPPAINTSTRUCT) | cosmetic | Begin painting; can stub |
| 60 | `CallNextHookEx` | LRESULT CallNextHookEx(HHOOK, int, WPARAM, LPARAM) | optional | Hook chain; can stub |
| 61 | `CallWindowProcA` | LRESULT CallWindowProcA(WNDPROCA, HWND, UINT, WPARAM, LPARAM) | critical | Call original window proc |
| 62 | `CheckDlgButton` | BOOL CheckDlgButton(HWND, int, UINT) | cosmetic | Dialog button state |
| 63 | `ClipCursor` | BOOL ClipCursor(const RECT*) | cosmetic | Restrict cursor to window; can stub |
| 64 | `CreateDialogParamA` | HWND CreateDialogParamA(HINSTANCE, LPCSTR, HWND, DLGPROC, LPARAM) | critical | Create dialog boxes (options menus) |
| 65 | `CreateWindowExA` | HWND CreateWindowExA(DWORD, LPCSTR, LPCSTR, DWORD, int, int, int, int, HWND, HMENU, HINSTANCE, LPVOID) | critical | Create main game window |
| 66 | `DefWindowProcA` | LRESULT DefWindowProcA(HWND, UINT, WPARAM, LPARAM) | critical | Default window procedure |
| 67 | `DestroyWindow` | BOOL DestroyWindow(HWND) | critical | Close window |
| 68 | `DispatchMessageA` | LRESULT DispatchMessageA(const MSG*) | critical | Dispatch message to window proc |
| 69 | `EnableWindow` | BOOL EnableWindow(HWND, BOOL) | cosmetic | Enable/disable window; can stub |
| 70 | `EndPaint` | BOOL EndPaint(HWND, const PAINTSTRUCT*) | cosmetic | End painting; can stub |
| 71 | `GetAsyncKeyState` | SHORT GetAsyncKeyState(int) | critical | Keyboard state polling (game input) |
| 72 | `GetClientRect` | BOOL GetClientRect(HWND, LPRECT) | critical | Get client area dimensions |
| 73 | `GetDC` | HDC GetDC(HWND) | critical | Get device context for drawing |
| 74 | `GetDesktopWindow` | HWND GetDesktopWindow(void) | cosmetic | Get desktop window handle |
| 75 | `GetDlgItem` | HWND GetDlgItem(HWND, int) | cosmetic | Get dialog control handle |
| 76 | `GetFocus` | HWND GetFocus(void) | cosmetic | Get focused window |
| 77 | `GetMessageA` | BOOL GetMessageA(LPMSG, HWND, UINT, UINT) | critical | Message pump (game loop) |
| 78 | `GetSystemMetrics` | int GetSystemMetrics(int) | critical | Screen resolution, DPI |
| 79 | `GetWindowLongA` | LONG GetWindowLongA(HWND, int) | critical | Get window properties |
| 80 | `GetWindowRect` | BOOL GetWindowRect(HWND, LPRECT) | critical | Get window position/size |
| 81 | `InvalidateRect` | BOOL InvalidateRect(HWND, const RECT*, BOOL) | cosmetic | Force repaint; can stub |
| 82 | `IsDialogMessageA` | BOOL IsDialogMessageA(HWND, LPMSG) | cosmetic | Route messages to dialogs |
| 83 | `IsDlgButtonChecked` | UINT IsDlgButtonChecked(HWND, int) | cosmetic | Check dialog button state |
| 84 | `IsWindow` | BOOL IsWindow(HWND) | critical | Validate window handle |
| 85 | `LoadCursorA` | HCURSOR LoadCursorA(HINSTANCE, LPCSTR) | cosmetic | Load cursor resource |
| 86 | `LoadIconA` | HICON LoadIconA(HINSTANCE, LPCSTR) | cosmetic | Load icon resource |
| 87 | `LoadStringA` | int LoadStringA(HINSTANCE, UINT, LPSTR, int) | cosmetic | Load string resource |
| 88 | `MapWindowPoints` | int MapWindowPoints(HWND, HWND, LPPOINT, UINT) | cosmetic | Coordinate transformation |
| 89 | `MessageBoxA` | int MessageBoxA(HWND, LPCSTR, LPCSTR, UINT) | cosmetic | Show error/info dialog |
| 90 | `MoveWindow` | BOOL MoveWindow(HWND, int, int, int, int, BOOL) | cosmetic | Reposition window |
| 91 | `PeekMessageA` | BOOL PeekMessageA(LPMSG, HWND, UINT, UINT, UINT) | critical | Non-blocking message check |
| 92 | `PostMessageA` | BOOL PostMessageA(HWND, UINT, WPARAM, LPARAM) | critical | Post message to queue |
| 93 | `PostQuitMessage` | void PostQuitMessage(int) | critical | Signal exit from message loop |
| 94 | `RegisterClassA` | ATOM RegisterClassA(const WNDCLASSA*) | critical | Register window class |
| 95 | `ReleaseDC` | int ReleaseDC(HWND, HDC) | critical | Release device context |
| 96 | `SendMessageA` | LRESULT SendMessageA(HWND, UINT, WPARAM, LPARAM) | critical | Synchronous message send |
| 97 | `SetCursor` | HCURSOR SetCursor(HCURSOR) | cosmetic | Set mouse cursor |
| 98 | `SetCursorPos` | BOOL SetCursorPos(int, int) | cosmetic | Position mouse cursor |
| 99 | `SetDlgItemTextA` | BOOL SetDlgItemTextA(HWND, int, LPCSTR) | cosmetic | Set dialog control text |
| 100 | `SetFocus` | HWND SetFocus(HWND) | cosmetic | Set input focus |
| 101 | `SetRect` | void SetRect(LPRECT, int, int, int, int) | critical | Initialize RECT structure |
| 102 | `SetWindowLongA` | LONG SetWindowLongA(HWND, int, LONG) | critical | Set window properties (subclassed proc) |
| 103 | `SetWindowPos` | BOOL SetWindowPos(HWND, HWND, int, int, int, int, UINT) | critical | Z-order, positioning |
| 104 | `SetWindowTextA` | BOOL SetWindowTextA(HWND, LPCSTR) | cosmetic | Set window title |
| 105 | `SetWindowsHookExA` | HHOOK SetWindowsHookExA(int, HOOKPROCP, HINSTANCE, DWORD) | optional | Install keyboard hook; can stub |
| 106 | `ShowWindow` | BOOL ShowWindow(HWND, int) | critical | Show/hide window |
| 107 | `SystemParametersInfoA` | BOOL SystemParametersInfoA(UINT, UINT, PVOID, UINT) | optional | System settings queries; can stub |
| 108 | `UnhookWindowsHookEx` | BOOL UnhookWindowsHookEx(HHOOK) | optional | Remove hook; can stub |
| 109 | `UpdateWindow` | BOOL UpdateWindow(HWND) | cosmetic | Force window redraw; can stub |
| 110 | `ValidateRect` | BOOL ValidateRect(HWND, const RECT*) | cosmetic | Mark area valid; can stub |

## DLL: USER32.DLL (2 functions) — *second import table entry (uppercase)*

| # | Function | Signature (stdcall, Win32) | Category | Notes |
|---|----------|---------------------------|----------|-------|
| 111 | `GetActiveWindow` | HWND GetActiveWindow(void) | cosmetic | Get active window; can stub |
| 112 | `wsprintfA` | int wsprintfA(LPSTR, LPCSTR, ...) | critical | String formatting (like sprintf for Windows) |

## DLL: KERNEL32.DLL (49 functions) — *second import table entry (uppercase)*

| # | Function | Signature (stdcall, Win32) | Category | Notes |
|---|----------|---------------------------|----------|-------|
| 113 | `CloseHandle` | BOOL CloseHandle(HANDLE) | critical | duplicate, different ordinal |
| 114 | `CreateDirectoryA` | BOOL CreateDirectoryA(LPCSTR, LPSECURITY_ATTRIBUTES) | optional | Create dirs (save paths); can stub |
| 115 | `CreateEventA` | HANDLE CreateEventA(LPSECURITY_ATTRIBUTES, BOOL, BOOL, LPCSTR) | critical | Synchronization event |
| 116 | `CreateFileA` | HANDLE CreateFileA(...) | critical | duplicate, different ordinal |
| 117 | `CreateMutexA` | HANDLE CreateMutexA(LPSECURITY_ATTRIBUTES, BOOL, LPCSTR) | optional | Single-instance mutex; can stub |
| 118 | `CreateThread` | HANDLE CreateThread(LPSECURITY_ATTRIBUTES, SIZE_T, LPTHREAD_START_ROUTINE, LPVOID, DWORD, LPDWORD) | critical | Spawn threads (sound, input) |
| 119 | `DeleteFileA` | BOOL DeleteFileA(LPCSTR) | optional | Delete temp/cache files; can stub |
| 120 | `DosDateTimeToFileTime` | BOOL DosDateTimeToFileTime(WORD, WORD, LPFILETIME) | optional | DOS date/time conversion |
| 121 | `ExitProcess` | void ExitProcess(UINT) | critical | Program termination |
| 122 | `ExitThread` | void ExitThread(DWORD) | critical | Thread termination |
| 123 | `FileTimeToDosDateTime` | BOOL FileTimeToDosDateTime(const FILETIME*, WORD*, WORD*) | optional | Reverse of above |
| 124 | `FileTimeToLocalFileTime` | BOOL FileTimeToLocalFileTime(const FILETIME*, LPFILETIME) | optional | Time zone conversion |
| 125 | `FindNextFileA` | BOOL FindNextFileA(HANDLE, LPWIN32_FIND_DATAA) | critical | Directory enumeration (WAD scanning) |
| 126 | `GetCPInfo` | BOOL GetCPInfo(UINT, LPCPINFO) | cosmetic | Code page info; stub TRUE |
| 127 | `GetCommandLineA` | LPSTR GetCommandLineA(void) | critical | Parse command-line args |
| 128 | `GetConsoleMode` | BOOL GetConsoleMode(HANDLE, LPDWORD) | optional | Console detection; can stub |
| 129 | `GetCurrentProcessId` | DWORD GetCurrentProcessId(void) | optional | Process ID; can stub |
| 130 | `GetCurrentThreadId` | DWORD GetCurrentThreadId(void) | critical | duplicate |
| 131 | `GetCurrentThread` | HANDLE GetCurrentThread(void) | optional | Pseudo-handle; can stub |
| 132 | `GetEnvironmentStrings` | LPTCH GetEnvironmentStrings(void) | cosmetic | Environment; can stub |
| 133 | `GetFileAttributesA` | DWORD GetFileAttributesA(LPCSTR) | critical | Check file existence |
| 134 | `GetFileSize` | DWORD GetFileSize(HANDLE, LPDWORD) | critical | File size queries |
| 135 | `GetFileTime` | BOOL GetFileTime(HANDLE, LPFILETIME, LPFILETIME, LPFILETIME) | optional | File timestamp; can stub |
| 136 | `GetFileType` | DWORD GetFileType(HANDLE) | optional | Detect console vs file; can stub |
| 137 | `GetLastError` | DWORD GetLastError(void) | critical | duplicate |
| 138 | `GetModuleFileNameA` | DWORD GetModuleFileNameA(HMODULE, LPTSTR, DWORD) | critical | Resolve EXE path |
| 139 | `GetModuleHandleA` | HMODULE GetModuleHandleA(LPCSTR) | critical | Get module handle |
| 140 | `GetProcAddress` | FARPROC GetProcAddress(HMODULE, LPCSTR) | critical | duplicate |
| 141 | `GetStdHandle` | HANDLE GetStdHandle(DWORD) | critical | Get stdin/stdout/stderr |
| 142 | `GetTimeZoneInformation` | DWORD GetTimeZoneInformation(LPTIME_ZONE_INFORMATION) | optional | Time zone; can stub |
| 143 | `GetVersion` | DWORD GetVersion(void) | critical | Windows version detection |
| 144 | `LoadLibraryA` | HMODULE LoadLibraryA(LPCSTR) | critical | duplicate |
| 145 | `LocalAlloc` | HLOCAL LocalAlloc(UINT, UINT) | critical | duplicate |
| 146 | `LocalFileTimeToFileTime` | BOOL LocalFileTimeToFileTime(const FILETIME*, LPFILETIME) | optional | Reverse of above |
| 147 | `LocalFree` | HLOCAL LocalFree(HLOCAL) | critical | duplicate |
| 148 | `ReadConsoleInputA` | BOOL ReadConsoleInputA(HANDLE, PINPUT_RECORD, DWORD, LPDWORD) | optional | Console key input; stub FALSE |
| 149 | `ReadFile` | BOOL ReadFile(HANDLE, LPVOID, DWORD, LPDWORD, LPOVERLAPPED) | critical | Read game files |
| 150 | `ReleaseMutex` | BOOL ReleaseMutex(HANDLE) | optional | Release mutex; can stub |
| 151 | `SetConsoleMode` | BOOL SetConsoleMode(HANDLE, DWORD) | optional | Console config; can stub |
| 152 | `SetEvent` | BOOL SetEvent(HANDLE) | critical | Signal event |
| 153 | `SetFilePointer` | DWORD SetFilePointer(HANDLE, LONG, PLONG, DWORD) | critical | Seek in files |
| 154 | `SetStdHandle` | BOOL SetStdHandle(DWORD, HANDLE) | optional | Redirect std handles; can stub |
| 155 | `TlsAlloc` | DWORD TlsAlloc(void) | critical | Thread-local storage allocation |
| 156 | `TlsFree` | BOOL TlsFree(DWORD) | critical | Free TLS slot |
| 157 | `TlsGetValue` | LPVOID TlsGetValue(DWORD) | critical | Read TLS value |
| 158 | `TlsSetValue` | BOOL TlsSetValue(DWORD, LPVOID) | critical | Write TLS value |
| 159 | `WaitForSingleObject` | DWORD WaitForSingleObject(HANDLE, DWORD) | critical | Wait on handle (events, mutexes) |
| 160 | `WriteConsoleA` | BOOL WriteConsoleA(HANDLE, const VOID*, DWORD, LPDWORD, LPVOID) | cosmetic | Console output; can stub |
| 161 | `WriteFile` | BOOL WriteFile(...) | critical | duplicate |

## DLL: DPLAY.dll (1 function) — *ordinal import #1*

| # | Function | Signature (stdcall, Win32) | Category | Notes |
|---|----------|---------------------------|----------|-------|
| 162 | `Ordinal #1` | Unknown — ordinal-based import | optional | DirectPlay init for multiplayer. Can stub returning DP_OK/NULL. **Needs investigation** |

## DLL: DDRAW.dll (1 function) — *DirectDraw 1.x*

| # | Function | Signature (stdcall, Win32) | Category | Notes |
|---|----------|---------------------------|----------|-------|
| 163 | `DirectDrawCreate` | HRESULT DirectDrawCreate(GUID FAR *lpGuid, LPDIRECTDRAW FAR *lplpDD, IUnknown FAR *pUnkOuter) | critical | **All rendering through this** — creates IDirectDraw interface. Game calls methods via vtable (CreateSurface, Flip, Blt, etc.). Stub needs mock vtable |

## DLL: DSOUND.dll (1 function) — *DirectSound 1.x*

| # | Function | Signature (stdcall, Win32) | Category | Notes |
|---|----------|---------------------------|----------|-------|
| 164 | `DirectSoundCreate` | HRESULT DirectSoundCreate(LPCGUID lpGuid, LPDIRECTSOUND FAR *lplpDS, IUnknown FAR *pUnkOuter) | critical | **All audio through this** — creates IDirectSound interface. Game creates buffers via vtable. Stub needs mock vtable |

---

## Summary by Category

| Category | Count | Description |
|----------|-------|-------------|
| **critical** | 99 | Must be implemented or game won't function |
| **optional** | 26 | Can be stubbed with default/NULL returns |
| **cosmetic** | 39 | UI polish — can be stubbed |

## Per-DLL Function Counts

| DLL | Functions |
|-----|-----------|
| KERNEL32.dll (first) | 20 |
| ADVAPI32.dll | 5 |
| WINMM.dll | 15 |
| GDI32.dll | 16 |
| USER32.dll | 54 |
| USER32.DLL | 2 |
| KERNEL32.DLL | 49 |
| DPLAY.dll | 1 |
| DDRAW.dll | 1 |
| DSOUND.dll | 1 |
| **Total** | **164** |

## Key Observations

1. **DirectDraw & DirectSound**: Only the factory functions are imported. All other methods are vtable calls.
2. **DPLAY ordinal #1**: Ordinal-based import. Needs investigation for the actual function name.
3. **Two KERNEL32 entries**: Lowercase provides core loader functions; uppercase provides game-specific kernel functions.
4. **Two USER32 entries**: Lowercase has the bulk (36 functions); uppercase adds 2 more (GetActiveWindow, wsprintfA).
5. **WINMM is extensive**: 15 functions for MIDI and joystick input.
6. **No MAPI32, COMDLG32, OLE32**: DOOM95 keeps dependencies minimal — only 8 DLLs.