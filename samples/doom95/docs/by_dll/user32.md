# user32.dll — 56 unique functions

(54 from `user32.dll` first entry + 2 from `user32.DLL` second entry)

## Window Lifecycle (8)

| Function | Category | SDL2 Mapping | Notes |
|----------|----------|-------------|-------|
| `CreateWindowExA` | critical | `SDL_CreateWindow()` | Map DWORD style→Uint32 flags. Allocate HWND from handle table. |
| `DestroyWindow` | critical | `SDL_DestroyWindow()` | Free HWND. Return TRUE. |
| `ShowWindow` | critical | `SDL_ShowWindow()` / `SDL_HideWindow()` | nCmdShow: SW_SHOW→Show, SW_HIDE→Hide |
| `SetWindowPos` | critical | `SDL_SetWindowPosition()` + `SDL_SetWindowSize()` | Split x,y,w,h. Ignore uFlags except SWP_NOSIZE/SWP_NOMOVE. |
| `MoveWindow` | cosmetic | Same as SetWindowPos | Same split |
| `SetWindowTextA` | cosmetic | `SDL_SetWindowTitle()` | Trivial string copy |
| `GetWindowRect` | critical | `SDL_GetWindowPosition()` + `SDL_GetWindowSize()` | Fill RECT |
| `GetClientRect` | critical | `SDL_GetWindowSize()` | Fill RECT with (0,0,w,h) |

## Window Properties (6)

| Function | Category | SDL2 Mapping | Notes |
|----------|----------|-------------|-------|
| `RegisterClassA` | critical | Store WNDCLASSA+WNDPROC in hashmap | Keyed by class name. No SDL2 equivalent. |
| `GetWindowLongA` | critical | Internal hashmap (HWND→LONG) | GWL_WNDPROC for subclassing. No SDL2 equivalent. |
| `SetWindowLongA` | critical | Internal hashmap (HWND→LONG) | Must handle GWL_WNDPROC for subclassing |
| `IsWindow` | critical | `SDL_HasWindowFlags()` or internal check | TRUE if handle exists in window table |
| `AdjustWindowRect` | trivial | Stub (always TRUE) | *rect = rect_original. SDL manages frame size. |
| `AdjustWindowRectEx` | trivial | Stub (always TRUE) | Same as above |

## Message Loop (9)

| Function | Category | SDL2 Mapping | Notes |
|----------|----------|-------------|-------|
| `GetMessageA` | critical | `SDL_WaitEvent()` | Blocking poll. Translate SDL_Event→MSG. |
| `PeekMessageA` | critical | `SDL_PeepEvents()` with SDL_GETEVENT | Non-blocking. 1=found, 0=empty |
| `DispatchMessageA` | critical | Call stored WNDPROC | Route to CallWindowProcA with window's proc |
| `PostMessageA` | critical | `SDL_PushEvent()` | Translate MSG→SDL_Event, push to queue |
| `PostQuitMessage` | critical | `SDL_PushEvent()` with SDL_QUIT | Push quit event |
| `SendMessageA` | critical | Direct WNDPROC call | Sync. Special cases: WM_GETTEXT→title, WM_SETTEXT→title |
| `DefWindowProcA` | critical | Stub (return 0) | Default = no-op under SDL2 |
| `CallWindowProcA` | critical | Direct function pointer call | Call stored proc pointer from SetWindowLongA |
| `TranslateMessage` | (not imported) | | May be needed for WM_KEYUP synthesis |

## Dialog System (7)

| Function | Category | SDL2 Mapping | Notes |
|----------|----------|-------------|-------|
| `CreateDialogParamA` | critical | Internal dialog manager | **No SDL2 equivalent.** Parse dialog resource from PE .rsrc. Store control state. Return mock HWND. Game uses for options menus. |
| `IsDialogMessageA` | cosmetic | Internal dialog manager | Route to dialog WNDPROC if active |
| `GetDlgItem` | cosmetic | Internal dialog control table | Lookup control handle from dialog+ID |
| `CheckDlgButton` | cosmetic | Internal dialog state | Store checkbox state per control |
| `IsDlgButtonChecked` | cosmetic | Internal dialog state | Return checkbox state |
| `SetDlgItemTextA` | cosmetic | Internal dialog state | Set text label per control |
| `MessageBoxA` | cosmetic | `SDL_ShowSimpleMessageBox()` | Direct map |

## Keyboard / Input (4)

| Function | Category | SDL2 Mapping | Notes |
|----------|----------|-------------|-------|
| `GetAsyncKeyState` | critical | `SDL_GetKeyboardState()` | Map int VK→SDL_Scancode. Return top bit (pressed) + high bit (toggle) in SHORT |
| `LoadCursorA` | cosmetic | `SDL_CreateSystemCursor()` | Map IDC_ARROW/IDC_CROSS→SDL_SYSTEM_CURSOR_* |
| `SetCursor` | cosmetic | `SDL_SetCursor()` | Map HCURSOR→SDL_Cursor* |
| `SetCursorPos` | cosmetic | `SDL_WarpMouseInWindow()` | Map (x,y) to window-local coords |

## Painting (6)

| Function | Category | SDL2 Mapping | Notes |
|----------|----------|-------------|-------|
| `GetDC` | critical | `SDL_GetWindowSurface()` wrapper | Return mock HDC wrapping SDL_Surface* |
| `ReleaseDC` | critical | No-op | HDC not reference-counted in SDL. Return 1 |
| `BeginPaint` | cosmetic | Stub | Return mock HDC (same as GetDC) |
| `EndPaint` | cosmetic | Stub | Return TRUE |
| `InvalidateRect` | cosmetic | Stub | Return TRUE |
| `ValidateRect` | cosmetic | Stub | Return TRUE |

## Window Utility (14)

| Function | Category | SDL2 Mapping | Notes |
|----------|----------|-------------|-------|
| `UpdateWindow` | cosmetic | Stub (return TRUE) | Force window redraw |
| `SetRect` | critical | Manual struct fill | r.left=x, r.top=y, r.right=x2, r.bottom=y2 |
| `GetSystemMetrics` | critical | `SDL_GetCurrentDisplayMode()` or hardcoded | SM_CXSCREEN→display width, SM_CYSCREEN→height, others→0 |
| `GetDesktopWindow` | cosmetic | Return sentinel HWND | Special handle for "desktop" |
| `GetActiveWindow` | cosmetic | Return current SDL_Window* as HWND | Main game window |
| `GetFocus` | cosmetic | Return current SDL_Window* as HWND | Same |
| `SetFocus` | cosmetic | Stub (return previous HWND) | No SDL2 focus equivalent needed |
| `EnableWindow` | cosmetic | Stub (return TRUE) | Irrelevant under SDL2 |
| `ClipCursor` | cosmetic | Stub (return TRUE) | No SDL2 equivalent |
| `LoadIconA` | cosmetic | Stub (return sentinel HICON) | Not used by game rendering |
| `LoadStringA` | cosmetic | Internal string table lookup | Store strings indexed by ID |
| `SetWindowsHookExA` | optional | Stub (return NULL) | Keyboard hook for key-repeat; replace with SDL_GetKeyState() |
| `CallNextHookEx` | optional | Stub (return 0) | No-op if hook chain empty |
| `UnhookWindowsHookEx` | optional | Stub (return TRUE) | No-op |
| `SystemParametersInfoA` | optional | Stub (return TRUE) | Game queries mouse speed/double-click time |
| `MapWindowPoints` | cosmetic | Stub (identity transform) | No window hierarchy under SDL2 |

## Misc (2)

| Function | Category | SDL2 Mapping | Notes |
|----------|----------|-------------|-------|
| `wsprintfA` | critical | `snprintf()` (C stdlib) | Win32-style vsprintf wrapper. Translate %%→% |

---

## Custom Implementation Required

- **HWND / HDC / HCURSOR type wrappers** — internal opaque structs with integer IDs mapping to SDL2 objects
- **WNDPROC table** — RegisterClassA + SetWindowLongA(GWL_WNDPROC) store function pointers per window; SendMessageA/DispatchMessageA dispatch through this table
- **Dialog system** — CreateDialogParamA + 6 helpers: parse dialog resource, store control state in memory, route messages. No rendering needed if game draws UI via DDraw/GDI
