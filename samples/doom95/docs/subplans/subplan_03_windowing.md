# Subplan 3: Windowing

**Goal**: Window creation, message loop, WNDPROC dispatch, keyboard/mouse input.

**Outcome**: A test PE with `RegisterClassA → CreateWindowExA → GetMessage loop → closes on click` runs under my_wine.

---

## Tasks

### 3.1 user32_types.h
- [x] Create `include/user32_types.h` with:
  - `HWND` (handle manager ID, type 0x01)
  - `HDC` (handle manager ID, type 0x02)
  - `HCURSOR` (handle manager ID, type 0x06)
  - `HICON` (handle manager ID, type 0x06)
  - `MSG` struct: `hwnd`, `message`, `wParam`, `lParam`, `time`, `pt`
  - `RECT` struct: `left`, `top`, `right`, `bottom`
  - `WNDCLASSA` struct: `style`, `lpfnWndProc`, `cbClsExtra`, `cbWndExtra`, `hInstance`, `hIcon`, `hCursor`, `hbrBackground`, `lpszMenuName`, `lpszClassName`
  - `PAINTSTRUCT` struct: `hdc`, `fErase`, `rcPaint`, `fRestore`, `fPaintValidateRect`
  - `WNDPROC` typedef: `LRESULT CALLBACK(HWND, UINT, WPARAM, LPARAM)`
  - Windows message constants: `WM_CREATE`(0x0001), `WM_DESTROY`(0x0002), `WM_MOVE`(0x0003), `WM_SIZE`(0x0005), `WM_ACTIVATE`(0x0006), `WM_SETFOCUS`(0x0007), `WM_KILLFOCUS`(0x0008), `WM_CLOSE`(0x0010), `WM_QUIT`(0x001B), `WM_SYSKEYDOWN`(0x0104), `WM_SYSKEYUP`(0x0105), `WM_KEYDOWN`(0x0100), `WM_KEYUP`(0x0101), `WM_CHAR`(0x0102), `WM_MOUSEMOVE`(0x0200), `WM_LBUTTONDOWN`(0x0201), `WM_LBUTTONUP`(0x0202), `WM_LBUTTONDBLCLK`(0x0203), `WM_RBUTTONDOWN`(0x0204), `WM_RBUTTONUP`(0x0205), `WM_RBUTTONDBLCLK`(0x0206), `WM_MBUTTONDOWN`(0x0207), `WM_MBUTTONUP`(0x0208), `WM_MBUTTONDBLCLK`(0x0209), `WM_MOUSEWHEEL`(0x020A), `WM_SYSCOMMAND`(0x0112), `WM_SETCURSOR`(0x0020), `WM_PAINT`(0x000F), `WM_GETTEXT`(0x000D), `WM_SETTEXT`(0x0012), `WM_GETMINMAXINFO`(0x0024), `WM_DISPLAYCHANGE`(0x007E)
  - Show window constants: `SW_HIDE`(0), `SW_SHOWNORMAL`(1), `SW_SHOWMINIMIZED`(2), `SW_SHOWMAXIMIZED`(3), `SW_SHOW`(5)
  - Window styles: `WS_OVERLAPPEDWINDOW`(0x00000000 | WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_THICKFRAME | WS_MINIMIZEBOX | WS_MAXIMIZEBOX), `WS_VISIBLE`(0x10000000)
  - `GWL_WNDPROC`(-4) for `GetWindowLongA`/`SetWindowLongA`

### 3.2 user32_window.c (27 functions)
- [x] `RegisterClassA` → store `WNDCLASSA` + `WNDPROC` in internal hash (keyed by class name)
- [x] `CreateWindowExA` → `rb_window_create()` + allocate `HWND` from handle manager + store style/params in `window_entry`
- [x] `DestroyWindow` → `rb_window_destroy()` + free `HWND`
- [x] `ShowWindow` → map `nCmdShow` to `rb_window_show()` / `SDL_ShowWindow()` / `SDL_HideWindow()` / `SDL_RestoreWindow()`
- [x] `SetWindowPos` → `rb_window_set_position()` + `rb_window_set_size()`
- [x] `MoveWindow` → same as `SetWindowPos`
- [x] `SetWindowTextA` → `rb_window_set_title()`
- [x] `GetWindowRect` → `rb_window_get_rect()` → fill `RECT`
- [x] `GetClientRect` → `rb_window_get_client_rect()` → fill `RECT` with `(0,0,w,h)`
- [x] `GetWindowLongA` / `SetWindowLongA` → internal `window_entry` field access (handle `GWL_WNDPROC` for subclassing)
- [x] `IsWindow` → return `TRUE` if `HWND` exists in window table
- [x] `EnableWindow` → `return TRUE`
- [x] `GetDesktopWindow` → return sentinel `HWND`
- [x] `GetActiveWindow` → return current main `HWND`
- [x] `GetFocus` → return current main `HWND`
- [x] `SetFocus` → return previous `HWND`
- [x] `UpdateWindow` → `return TRUE`
- [x] `InvalidateRect` → `return TRUE`
- [x] `ValidateRect` → `return TRUE`
- [x] `BeginPaint` → return mock `HDC` (same as `GetDC`)
- [x] `EndPaint` → `return TRUE`
- [x] `MapWindowPoints` → identity transform; return `nCount`
- [x] `GetSystemMetrics` → map index to `rb_window_get_client_rect()` or hardcoded value:
  - `SM_CXSCREEN`(0) → display width, `SM_CYSCREEN`(1) → display height
  - `SM_CXBORDER`(2) → 1, `SM_CYBORDER`(3) → 1
  - `SM_CXFULLSCREEN`(16) → display width, `SM_CYFULLSCREEN`(17) → display height
  - All others → 0
- [x] `AdjustWindowRect` / `AdjustWindowRectEx` → `*rect = rect_original; return TRUE`
- [x] `GetDC` → `rb_window_get_dc()` (via handle manager)
- [x] `ReleaseDC` → `rb_window_release_dc()` → `return 1`

### 3.3 user32_message.c (13 functions)
- [x] `GetMessageA` → `rb_event_wait()` → blocking wait, returns 0 on `WM_QUIT`
- [x] `PeekMessageA` → `rb_event_peek()` → non-blocking peek
- [x] `DispatchMessageA` → call `WNDPROC` stored for target `HWND`
- [x] `PostMessageA` → `rb_event_push()` → translate `MSG` to `SDL_Event` and push
- [x] `PostQuitMessage` → `rb_event_push()` with `WM_QUIT`
- [x] `SendMessageA` → direct `WNDPROC` call; special cases:
  - `WM_GETTEXT` → copy window title to buffer
  - `WM_SETTEXT` → `rb_window_set_title()`
  - `WM_GETMINMAXINFO` → fill struct with current size
- [x] `DefWindowProcA` → `return 0`
- [x] `CallWindowProcA` → direct function pointer call
- [x] `SetWindowsHookExA` → `return NULL` (no hook chain)
- [x] `UnhookWindowsHookEx` → `return TRUE`
- [x] `CallNextHookEx` → `return 0`
- [x] `SystemParametersInfoA` → `return TRUE`

### 3.4 user32_input.c (8 functions)
- [x] `GetAsyncKeyState` → `rb_keyboard_get_async_state()` + full VK→Scancode table (~200 entries)
- [x] `LoadCursorA` → `rb_cursor_create()` (map `IDC_ARROW`→0, `IDC_CROSS`→1, `IDC_HAND`→9, `IDC_IBEAM`→2)
- [x] `SetCursor` → `rb_window_set_cursor()`
- [x] `SetCursorPos` → `rb_window_warp_mouse()`
- [x] `ClipCursor` → `return TRUE`
- [x] `LoadIconA` → return sentinel `HICON`
- [x] `wsprintfA` → `vsprintf()` wrapper
- [x] `SetRect` → `r->left=x; r->top=y; r->right=x2; r->bottom=y2;`

### 3.5 import_table.c
- [x] Add all 48 user32.dll function entries to `import_table.c`
- [x] Format: `{ "user32.dll", "FunctionName", (void*)stub_FunctionName }`
- [x] Also add `user32.DLL` entries (case-insensitive matching in resolver)

### 3.6 Test
- [x] Compile a test PE that:
  1. Calls `RegisterClassA` with a custom `WNDPROC`
  2. Calls `CreateWindowExA(0, "TestClass", "Test", WS_VISIBLE, 0, 0, 320, 200, 0, 0, hInst, 0)`
  3. Enters `while (GetMessage(&msg, 0, 0, 0)) { TranslateMessage(&msg); DispatchMessage(&msg); }`
  4. `WNDPROC` handles `WM_DESTROY` → `PostQuitMessage(0)`
- [x] Expected: SDL2 window appears with title "Test" at 320x200. Clicking close button → window closes → loop exits → process terminates cleanly.

Verification uses the existing graphical PE samples:
- `samples/sdl2_window/` — minimal `RegisterClassA` + `CreateWindowExA` + `GetMessageA` loop
- `samples/sdl2_window_closewindow/` — same loop with harness-driven close-button verification

---

## Files
| File | Action |
|------|--------|
| `include/user32_types.h` | **New** (~150 lines) |
| `src/stubs/user32_window.c` | **New** (~400 lines) |
| `src/stubs/user32_message.c` | **New** (~400 lines) |
| `src/stubs/user32_input.c` | **New** (~150 lines) |
| `src/loader/import_table.c` | Edit: add 48 user32.dll entries |

**~1,100 lines, ~4-5 days**
