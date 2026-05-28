# Windows API Stubs — Reference

Reference for all Windows API stub implementations in `src/msvcrt/`. ~80 files
covering ntdll, kernel32, user32, msvcrt CRT, ddraw, dsound, gdi32, winmm,
advapi32, and DirectPlay.

---

## Overview

The `src/msvcrt/` directory contains all the Windows API translation layer.
Every exported function uses the `KERNEL32_STUB` macro (`handler_abi.h`) which
resolves to `__attribute__((ms_abi, used))` on x86_64 (matching the Microsoft
x64 calling convention) or `__attribute__((used))` on i386. Internal helpers
use `HANDLER` (System V ABI).

```
src/msvcrt/
├── handler_abi.h              # WINE_STUB / KERNEL32_STUB / HANDLER macros
│
├── ntdll_*                    # NT subsystem call handlers (HANDLER ABI)
├── kernel32_*                 # Process, file, memory, sync, console, string
├── user32_*                   # Window lifecycle, messages, dialogs, input
├── ddraw_*                    # DirectDraw for DOOM95
├── dsound_*                   # DirectSound for DOOM95
├── gdi32_doom95.c             # GDI stubs (Doom95-specific)
├── winmm_doom95*              # Multimedia + MIDI backend (Doom95-specific)
├── advapi32_registry.c        # Registry stubs
├── dplay_stub.c               # DirectPlay stubs
├── handle_manager*             # Windows handle table
├── crt_*                      # CRT globals, stdio, stdlib, file, startup, refptrs
├── resource_win32*            # PE resource loading
└── launcher_*                  # Launcher UI-specific stubs
```

Each library cluster has a private header (e.g. `ntdll_priv.h`, `kernel32_priv.h`)
that re-exports public types and declares internal globals, helper functions, and
shared state. All `ntdll_*` files include `ntdll_priv.h`; all `kernel32_*` files
include `kernel32_priv.h`; etc.

---

## ntdll — NT Subsystem Calls

NT-level syscall handlers. These are compiled with `HANDLER` (System V ABI)
because the syscall dispatcher calls them from generated thunk code.

| File | Handlers |
|------|----------|
| [`ntdll_handle.c`](../../src/msvcrt/ntdll_handle.c) | Handle table infrastructure: `handle_to_fd()`, `fd_to_handle()`, `free_handle()`, `NtClose`. Maps Windows handle indices to Linux file descriptors. Handles STDIN/STDOUT/STDERR pseudo-handles (0, 1, 2). |
| [`ntdll_io.c`](../../src/msvcrt/ntdll_io.c) | `NtWriteFile`, `NtReadFile`, `NtOpenFile`. Translate Windows desired_access flags to Linux open flags. Write/read via `INLINE_SYSCALL_WRITE`/`READ`. |
| [`ntdll_memory.c`](../../src/msvcrt/ntdll_memory.c) | `NtAllocateVirtualMemory`, `NtFreeVirtualMemory`, `NtCreateSection`, `NtMapViewOfSection`, `NtUnmapViewOfSection`, `NtProtectVirtualMemory`. Map `PAGE_*` to `PROT_*`, `mmap`/`munmap`/`mprotect`. Defines `g_ko` (kernel objects global). |
| [`ntdll_process.c`](../../src/msvcrt/ntdll_process.c) | `NtTerminateProcess`, `NtCallbackReturn`, `NtQueryInformationProcess`. `NtTerminateProcess` uses `INLINE_SYSCALL_EXIT_GROUP` (not `sys_exit`) to tear down all threads. Returns `PROCESS_BASIC_INFORMATION` for QI. |
| [`ntdll_synchronization.c`](../../src/msvcrt/ntdll_synchronization.c) | `NtSetEvent`, `NtResetEvent`, `NtWaitForSingleObject`, `NtCreateMutex`, `NtReleaseMutex`, `NtCreateSemaphore`, `NtReleaseSemaphore`. Uses `g_ko` event/mutex/semaphore tables with spinlock protection. `WaitForSingleObject` supports timeout via `nanosleep`. |
| [`ntdll_time.c`](../../src/msvcrt/ntdll_time.c) | `NtQuerySystemTime`, `NtQueryPerformanceCounter`, `NtQueryPerformanceFrequency`, `NtDelayExecution`. `QuerySystemTime` converts Unix epoch to Windows FILETIME (100ns since 1601). `QueryPerformanceCounter` uses `CLOCK_MONOTONIC`. |
| [`ntdll_objects.c`](../../src/msvcrt/ntdll_objects.c) | `NtCreateEvent`, `NtCreateThreadEx`, `NtGetContextThread`, `NtSetContextThread`. `NtCreateThreadEx` creates a pthread via shared memory page for args. Event creation allocated from `g_ko` event table. |

**Shared state:** `ntdll_priv.h` declares `g_ko` (kernel objects: sections, views, events, mutexes, semaphores, threads), `handle_to_fd()`, `fd_to_handle()`, `free_handle()`, `map_protect()`, `find_view()`.

---

## kernel32 — Process, File, Memory, Console, String, Sync

All exported functions use `KERNEL32_STUB` (stdcall/ms_abi). The private header
`kernel32_priv.h` declares `g_last_error` (thread-local last error), helper
functions, and `WIN32_FIND_DATAA_WINE`.

### Console

| File | Functions |
|------|-----------|
| [`kernel32_console.c`](../../src/msvcrt/kernel32_console.c) | `GetStdHandle`, `GetConsoleMode`, `SetConsoleMode`, `SetStdHandle`, `WriteConsoleA`, `ReadConsoleInputA`, `WriteFile`, `ReadFile`, `FlushFileBuffers`. `WriteFile`/`ReadFile` use direct `INLINE_SYSCALL_WRITE`/`READ` to avoid ABI mismatch with ntdll handlers. `GetStdHandle` returns pseudo-handles (STDIN_HANDLE=0, STDOUT_HANDLE=1, STDERR_HANDLE=2). |

### Process

| File | Functions |
|------|-----------|
| [`kernel32_process.c`](../../src/msvcrt/kernel32_process.c) | `ExitProcess` (→ `NtTerminateProcess`), `GetStartupInfoA`, `SetUnhandledExceptionFilter`, `Sleep` (via `nanosleep`), `CreateThread`. `ExitProcess` validates the thunk exists before calling `NtTerminateProcess`. |

### Module

| File | Functions |
|------|-----------|
| [`kernel32_module.c`](../../src/msvcrt/kernel32_module.c) | `LoadLibraryA`, `LoadLibraryExA`, `FreeLibrary`, `GetProcAddress`, `GetModuleHandleA`, `GetModuleFileNameA`, `GetModuleHandleExA`. Maintains builtin module table (kernel32.dll through comctl32.dll with handles 0xffff0001-0x0c). `FreeLibrary` invokes `DLLMain(DLL_PROCESS_DETACH)` via inline assembly. `GetProcAddress` searches the IAT of the mapped module. |

### Misc

| File | Functions |
|------|-----------|
| [`kernel32_misc.c`](../../src/msvcrt/kernel32_misc.c) | `GetLastError`, `SetLastError`, `IsTNT`, `GetCommandLineA`, `GetEnvironmentStringsA`, `__C_specific_handler` (SEH — returns `ExceptionContinueSearch`), `InitializeSListHead`, `InterlockedCompareExchange`, `InterlockedExchange`, `GetLongJumpBuffer`. Defines `g_last_error` global. |

### Path

| File | Functions |
|------|-----------|
| [`kernel32_path.c`](../../src/msvcrt/kernel32_path.c) | `GetCurrentDirectoryA`, `SetCurrentDirectoryA`, `GetFullPathNameA`, `CreateDirectoryA`, `GetFileAttributesA`, `SetFileAttributesA`, `DeleteFileA`, `CreateDirectoryExA`. Maintains cached current directory. `wine_resolve_path()` converts Windows-style paths. Uses `int $0x80`/`syscall` directly for `getcwd`, `chdir`, `stat`, `mkdir`, `unlink`. |

### File

| File | Functions |
|------|-----------|
| [`kernel32_file.c`](../../src/msvcrt/kernel32_file.c) | `CloseHandle`, `CreateFileA`, `SetFilePointer`, `SetFileEndOfPointer`, `GetFileSize`, `GetFileType`, `GetFileAttributesExA`, `FindFirstFileA`, `FindNextFileA`, `FindClose`, `CreateFileMappingA`, `MapViewOfFile`, `UnmapViewOfFile`. `CreateFileA` maps Windows access/disposition flags to Linux `open()`. 32-bit fallback array `createfile_fds_32[64]` for handle storage. File mapping via `NtCreateSection`/`NtMapViewOfSection`. |

### Memory

| File | Functions |
|------|-----------|
| [`kernel32_memory.c`](../../src/msvcrt/kernel32_memory.c) | `LocalAlloc`, `GlobalAlloc`, `LocalFree`, `GlobalFree`, `HeapCreate`, `HeapAlloc`, `HeapFree`, `HeapSize`, `HeapReAlloc`, `GetProcessHeap`, `VirtualAlloc`, `VirtualFree`, `VirtualProtect`, `VirtualQuery`, `VirtualLock`. `VirtualAlloc` delegates to `NtAllocateVirtualMemory`. `HeapAlloc`/`HeapFree` use musl `malloc`/`free` on PE32+ or custom mmap allocator on PE32. Tracks `VirtualAlloc` allocations for `VirtualFree(MEM_RELEASE, size=0)`. |

### String

| File | Functions |
|------|-----------|
| [`kernel32_string.c`](../../src/msvcrt/kernel32_string.c) | `lstrlenA`, `lstrcpyA`, `lstrcatA`, `IsDBCSLeadByteEx`, `MultiByteToWideChar`, `WideCharToMultiByte`, `CharLowerA`, `CharUpperA`, `CharLowerBuffA`, `CharUpperBuffA`, `CharNextA`, `CharPrevA`. Pure-C implementations — no libc dependency. |

### Sync

| File | Functions |
|------|-----------|
| [`kernel32_sync.c`](../../src/msvcrt/kernel32_sync.c) | `CreateEventA`, `SetEvent`, `ResetEvent`, `WaitForSingleObject`, `CreateMutexA`, `ReleaseMutex`, `InitializeCriticalSection`, `EnterCriticalSection`, `LeaveCriticalSection`, `DeleteCriticalSection`. All map to the `Nt*` syscall handlers in `ntdll_synchronization.c`. `WaitForSingleObject` supports `INFINITE` timeout. |

### System

| File | Functions |
|------|-----------|
| [`kernel32_system.c`](../../src/msvcrt/kernel32_system.c) | `GetCurrentThreadId`, `GetCurrentProcessId`, `GetCurrentThread`, `GetSystemInfo`, `GetVersion`, `GetTimeZoneInformation`, `GetCPInfo`, `GetSystemTime`, `SystemTimeToFileTime`. Returns fixed values: thread=1, process=1, thread handle=-2 (pseudo-handle). `GetVersion` returns `0x80000004` (Windows 2000+ compatibility). |

### Time

| File | Functions |
|------|-----------|
| [`kernel32_time.c`](../../src/msvcrt/kernel32_time.c) | `GetTickCount` (via `CLOCK_MONOTONIC`), `GetSystemTimeAsFileTime`, `DosDateTimeToFileTime`, `FileTimeToDosDateTime`, `FileTimeToLocalFileTime`, `LocalFileTimeToFileTime`, `GetLocalTime`, `FileTimeToSystemTime`, `SystemTimeToFileTime`. Converts between Windows FILETIME (100ns since 1601), DOS FAT time, and `CLOCK_REALTIME`/`CLOCK_MONOTONIC`. |

### TLS

| File | Functions |
|------|-----------|
| [`kernel32_tls.c`](../../src/msvcrt/kernel32_tls.c) | `TlsAlloc`, `TlsFree`, `TlsSetValue`, `TlsGetValue`. Bitmap-based TLS slot allocation (max 64 slots). Simple array-backed storage — not actual thread-local storage. |

### Doom95-Specific

| File | Functions |
|------|-----------|
| [`kernel32_doom95.c`](../../src/msvcrt/kernel32_doom95.c) | `wine_build_doom95_basewad_path()`, `wine_doom95_seed_basewad_state()`. Game-specific helpers that construct the `DOOM1.WAD` path from the current directory and seed the game's basewad state at fixed offsets within the PE image. Header `kernel32_doom95.h` declares these helpers. |

### Resource

| File | Functions |
|------|-----------|
| [`kernel32_resource.c`](../../src/msvcrt/kernel32_resource.c) | `FindResourceA`, `SizeofResource`, `LoadResource`, `LockResource`. Thin wrapper around `resource_win32.c` for PE resource directory traversal. |

---

## user32 — Window Lifecycle, Messages, Dialogs, Input

All exported functions use `KERNEL32_STUB`. The private header `user32_priv.h`
declares `wine_window_entry` (per-window bookkeeping), `g_user32_live_windows`,
`g_user32_active_window`, `g_user32_focus_window`, and the `get_window_entry()`
inline accessor.

### Window Lifecycle

| File | Functions |
|------|-----------|
| [`user32_window.c`](../../src/msvcrt/user32_window.c) | `CreateWindowExA`, `DestroyWindow`. Entry points for window creation/destruction. `CreateWindowExA` looks up the registered class, allocates a `wine_window_entry`, calls the backend to create the SDL window, and registers the HWND in the handle manager. |
| [`user32_window_lifecycle.c`](../../src/msvcrt/user32_window_lifecycle.c) | Backend/bootstrap helpers: `user32_ensure_backend()`, `user32_init_window_entry()`, `user32_create_backend_window()`, `user32_fill_create_struct()`, `user32_finish_window_create()`, `user32_finish_window_destroy()`, `user32_cleanup_failed_create()`, `user32_heap_alloc()`, `user32_heap_free()`. On PE32, allocates from the process heap; on PE32+, uses `malloc`. |

### Window Ops

| File | Functions |
|------|-----------|
| [`user32_window_ops.c`](../../src/msvcrt/user32_window_ops.c) | `ShowWindow`, `SetWindowPos`, `MoveWindow`, `SetWindowTextA`, `GetWindowRect`, `GetClientRect`, `GetWindowLongA`, `SetWindowLongA`, `GetWindowLongPtrA`, `SetWindowLongPtrA`, `IsWindow`, `EnableWindow`, `GetDesktopWindow`, `GetActiveWindow`, `GetFocus`, `SetFocus`, `UpdateWindow`, `InvalidateRect`, `ValidateRect`. Delegates to `rb_window_*` backend calls. |

### Window State

| File | Functions |
|------|-----------|
| [`user32_window_state.c`](../../src/msvcrt/user32_window_state.c) | `GetWindowLongPtrA`, `SetWindowLongPtrA` — window property access (GWL_WNDPROC, GWL_STYLE, GWL_EXSTYLE, GWL_HINSTANCE, GWL_HWNDPARENT, GWL_USERDATA, GWL_ID). Reads/writes `wine_window_entry` fields. |

### Message Queue

| File | Functions |
|------|-----------|
| [`user32_message.c`](../../src/msvcrt/user32_message.c) | `GetMessageA`, `PeekMessageA`, `PostMessageA`, `PostQuitMessage`, `TranslateMessage`, `IsIconic`, `IsZoomed`, `ShowCursor`. `GetMessageA` blocks on `rb_event_wait()`; `PeekMessageA` calls `rb_event_peek()`. Translates backend messages to `MSG` structures. Excluded from default build (requires SDL2 backend symbols). |
| [`user32_message_queue.c`](../../src/msvcrt/user32_message_queue.c) | Internal queue/filter/quit helpers shared by the message-loop exports. Two-ring buffer queues: translated queue (64 entries) for `GetMessageA` and posted queue (64 entries) for `PostMessageA`. |

### Message Dispatch

| File | Functions |
|------|-----------|
| [`user32_message_dispatch.c`](../../src/msvcrt/user32_message_dispatch.c) | `DispatchMessageA`, `SendMessageA`, `SendMessageTimeoutA`, `DefWindowProcA`, `CallWindowProcA`, `PostThreadMessageA`. `DispatchMessageA` sends `WM_SETCURSOR` for mouse messages, routes close/destroy/quit to debug output, and calls the window procedure. `SendMessageA` is synchronous — calls the WNDPROC directly. |

### Message Hook

| File | Functions |
|------|-----------|
| [`user32_message_hook.c`](../../src/msvcrt/user32_message_hook.c) | `SetWindowsHookExA`, `UnhookWindowsHookEx`. Supports `WH_KEYBOARD` (hook ID 2) only. Stores the hook procedure and handle; `user32_call_keyboard_hook()` invokes it for keyboard messages. |

### Input

| File | Functions |
|------|-----------|
| [`user32_input.c`](../../src/msvcrt/user32_input.c) | `GetAsyncKeyState`, `LoadCursorA`, `SetCursor`, `SetCursorPos`, `ClipCursor`, `LoadIconA`, `wsprintfA`, `SetRect`. `GetAsyncKeyState` queries the backend keyboard state and tracks previous key-down state for the "just-pressed" bit. `LoadCursorA`/`LoadIconA` create SDL cursor/icon handles. Excluded from default build (requires SDL2). |

### Focus

| File | Functions |
|------|-----------|
| [`user32_focus.c`](../../src/msvcrt/user32_focus.c) | `user32_set_foreground_focus()`, `user32_activate_window_direct()`, `user32_update_window_ownership_after_destroy()`. Manages active/focus window policy: sends `WM_ACTIVATE`, `WM_SETFOCUS`, `WM_KILLFOCUS` transitions. Scans handle table for replacement window when current focus is destroyed. |

### Paint

| File | Functions |
|------|-----------|
| [`user32_paint.c`](../../src/msvcrt/user32_paint.c) | `BeginPaint`, `EndPaint`, `MapWindowPoints`, `GetDC`, `ReleaseDC`, `GetSystemMetrics`, `AdjustWindowRect`, `AdjustWindowRectEx`. `BeginPaint` allocates an HDC handle and sets `PAINTSTRUCT`. `GetSystemMetrics` handles `HORZRES` (8), `VERTRES` (10), `SM_CXSCREEN`, `SM_CYSCREEN`, etc. via `rb_display_get_size()`. |

### Dialog — Create

| File | Functions |
|------|-----------|
| [`user32_dialog_create.c`](../../src/msvcrt/user32_dialog_create.c) | `CreateDialogParamA`. Creates a dialog by looking up the template resource, ensuring the `MY_WINE_DIALOG` class exists, calling `CreateWindowExA` to create the window, setting the modal dlgproc, and sending `WM_INITDIALOG`. Triggers Doom95 autostart via `user32_dialog_try_doom95_autostart()`. |

### Dialog — Items

| File | Functions |
|------|-----------|
| [`user32_dialog_items.c`](../../src/msvcrt/user32_dialog_items.c) | `GetDlgItem`, `CheckDlgButton`, `IsDlgButtonChecked`, `SetDlgItemTextA`. Dialog item lookup and state mutation — maintains `dialog_item_state` entries per (dialog, control ID) pair. |

### Dialog — Lifecycle

| File | Functions |
|------|-----------|
| [`user32_dialog_lifecycle.c`](../../src/msvcrt/user32_dialog_lifecycle.c) | `user32_dialog_end()` / `EndDialog`, `user32_dialog_run_modal()` / `DialogBoxParamA`. Modal dialog completion: marks the dialog ended with a result code, calls `DestroyWindow`, clears modal state. |

### Dialog — Message

| File | Functions |
|------|-----------|
| [`user32_dialog_message.c`](../../src/msvcrt/user32_dialog_message.c) | `IsDialogMessageA`. Routes messages to the dialog procedure — looks up item by HWND, calls `dlgproc(hwnd, msg, wParam, lParam)`. |

### Dialog — Modal

| File | Functions |
|------|-----------|
| [`user32_dialog_modal.c`](../../src/msvcrt/user32_dialog_modal.c) | `user32_dialog_ensure_class()`, modal state tracking (`g_dialog_modals[16]`), `user32_dialog_set_modal_dlgproc()`, `user32_dialog_get_modal_dlgproc()`, `user32_dialog_mark_modal_end()`, `user32_dialog_run_modal_lifecycle()`, `user32_dialog_clear_modal()`. Registers the `MY_WINE_DIALOG` class with `DefWindowProcA` as the window procedure. |

### Dialog — State

| File | Functions |
|------|-----------|
| [`user32_dialog_state.c`](../../src/msvcrt/user32_dialog_state.c) | Dialog item state table (`g_dialog_items[64]`): `user32_dialog_find_item()`, `user32_dialog_find_item_by_handle()`, `user32_dialog_find_string()`, `user32_dialog_find_item_data_index()`. Handles assign auto-incrementing HWNDs from `0x40000000`. |

### Dialog — Controls

| File | Functions |
|------|-----------|
| [`user32_dialog_controls.c`](../../src/msvcrt/user32_dialog_controls.c) | `user32_dialog_send_control_message()` — handles `CB_*` (combo box), `LB_*` (list box), `UDM_*` (up-down/scroll) messages. Maintains `dialog_item_state` with string items, selection state, range, and position data. |

### Dialog — Doom95

| File | Functions |
|------|-----------|
| [`user32_dialog_doom95.c`](../../src/msvcrt/user32_dialog_doom95.c) | `user32_dialog_try_doom95_autostart()` — game-specific dialog initialization. Recognizes template ID `0x72` (Doom95 main menu), populates provider/WAD/map combo boxes, selects `DOOM1.WAD`, seeds basewad state, and auto-triggers the start button. |

### Dialog — Resources

| File | Functions |
|------|-----------|
| [`user32_dialog_resources.c`](../../src/msvcrt/user32_dialog_resources.c) | `LoadStringA` (loads string resources from PE), `MessageBoxA` (prints to stderr and returns IDOK). |

### Class Registry

| File | Functions |
|------|-----------|
| [`user32_class_registry.c`](../../src/msvcrt/user32_class_registry.c) | `RegisterClassA`, `user32_find_registered_class()`. Maintains a dynamic table of `WNDCLASSA` entries. Allocates via process heap (PE32) or `malloc` (PE32+). Handles string duplication for class names. |

### Weak Stubs

| File | Functions |
|------|-----------|
| [`user32_weak_stub.c`](../../src/msvcrt/user32_weak_stub.c) | `__attribute__((weak))` no-op fallbacks for all user32 exports. Used by the default `my_wine64` build (which doesn't link SDL2). When the real user32 files are compiled, their strong definitions override these. Every function returns a safe default (0, NULL, or FALSE). |

**Shared state:** `user32_priv.h` declares `wine_window_entry` (rb_window, wnd_proc, class_name, title, style, ex_style, user_data, hinstance, parent, menu, class_cursor, class_atom, client_rect, destroy_in_progress), `g_user32_live_windows`, `g_user32_window_create_attempted`, `g_user32_active_window`, `g_user32_focus_window`, and inline accessors. `user32_dialog_priv.h` declares `dialog_item_state`, `DLGPROC_WINE`, and dialog helper functions. `user32_message_priv.h` declares internal message-loop state.

---

## ddraw — DirectDraw (Doom95 Rendering)

COM-style interfaces implemented as C structs with vtables. All use `KERNEL32_STUB`
for COM methods. The private header `ddraw_priv.h` declares the internal structs
(`my_dd_t`, `my_surface_t`, `my_palette_t`, `my_clipper_t`) and vtable externs.

**Doom95-specific context:** This DirectDraw implementation targets DOOM95's
usage patterns: 8-bit palette surfaces, flip chains for double-buffered
rendering, and cooperative level management for exclusive fullscreen.

| File | Functions |
|------|-----------|
| [`ddraw_interface.c`](../../src/msvcrt/ddraw_interface.c) | `DirectDrawCreate`, `DirectDrawCreateEx`. COM core methods on `IDirectDraw`: `QueryInterface`, `AddRef`, `Release`, `Compact`, `GetCaps`, `GetClipper`, `SetClipper`, `GetColorKey`, `SetColorKey`, `GetCOoperativeLevel`, `SetCOoperativeLevel`, `GetDisplayMode`, `RestoreDisplayMode`, `EnumerateDisplayModes`, `GotoCooperativeLevel`, `AttachSurface`, `DetachSurface`, `DuplicateSurface`, `GetFourCCCodes`, `GetGDI Surface`, `RestoreAllSurfaces`, `WaitForVerticalBlank`, `GetMonitorFrequency`, `GetScanLine`, `GetVerticalBlankStatus`, `CreateSurface`. `DirectDrawCreate` creates the singleton `g_ddraw_instance`. |
| [`ddraw_core.c`](../../src/msvcrt/ddraw_core.c) | `ddraw_create_instance()`, `ddraw_query_interface()`, `ddraw_add_ref()`, `ddraw_release()`. Instance lifecycle: allocates `my_dd_t`, sets default mode (320×200, 8bpp), normal cooperative level. On release with ref_count=0, unlinks all surfaces, releases primary surface, destroys the backend window, and frees memory. |
| [`ddraw_clipper.c`](../../src/msvcrt/ddraw_clipper.c) | `ddraw_clipper_create()`, COM methods: `QueryInterface`, `AddRef`, `Release`, `GetClipList`, `GetHWnd`, `SetHWnd`, `IsClipListChanged`, `DestroyClipper`. Allocates from process heap (PE32) or `malloc`. |
| [`ddraw_mode.c`](../../src/msvcrt/ddraw_mode.c) | `ddraw_wait_for_vertical_blank()` (1ms `nanosleep`), `ddraw_get_monitor_frequency()` (returns 60), `ddraw_get_scan_line()` (fake incrementing counter), `ddraw_get_vertical_blank_status()` (fake boolean toggle), `ddraw_get_gdi_surface()`, `ddraw_get_display_mode()`, `ddraw_restore_display_mode()`, `ddraw_get_caps()`. |
| [`ddraw_palette.c`](../../src/msvcrt/ddraw_palette.c) | `ddraw_palette_create()`, COM methods: `QueryInterface`, `AddRef`, `Release`, `GetPaletteEntries`, `SetPaletteEntries`, `CreatePalette`. Creates `rb_palette_t` backend objects. Handles `DDPCAPS_1BIT` (2 colors) through `DDPCAPS_8BIT` (256 colors). |
| [`ddraw_surface.c`](../../src/msvcrt/ddraw_surface.c) | `ddraw_surface_alloc()`, COM methods: `QueryInterface`, `AddRef`, `Release`. Surface object allocation with owner-list registration and teardown (unlinks from owner list, releases palette/clipper, destroys backend surface). |
| [`ddraw_surface_create.c`](../../src/msvcrt/ddraw_surface_create.c) | `ddraw_create_surface_from_desc()`, `ddraw_create_flip_chain_surface()`, `ddraw_create_regular_surface()`, `ddraw_init_surface_geometry()`. Surface creation orchestration: parses guest descriptor, validates cooperative level, creates flip-chain (primary + backbuffer) or regular surfaces. |
| [`ddraw_surface_desc.c`](../../src/msvcrt/ddraw_surface_desc.c) | `ddraw_fill_surface_desc()`, `ddraw_init_display_mode_desc()`, `ddraw_parse_surface_desc()`. Converts between guest `DDSURFACEDESC2`/1.x layouts and normalized host data. Supports both the DDSURFACEDESC2 layout (caps at offset 0x68) and the DDraw 1.x layout (caps at offset 0x08). |
| [`ddraw_surface_ops.c`](../../src/msvcrt/ddraw_surface_ops.c) | Surface COM behavior beyond lifetime: `AddAttachedSurface`, `AddOverlayDirtyRect`, `Blt`, `BltBatch`, `BltFast`, `DeleteAttachedSurface`, `Flip`, `GetBltStatus`, `GetDC`, `GetOverlayPosition`, `GetPalette`, `SetPalette`, `GetSurfaceDesc`, `Lock`, `ReleaseDC`, `Restore`, `SetOverlayPosition`, `Unlock`. Delegates to `rb_surface_*` backend calls. Rect conversion `ddraw_rect_to_rb()`. |
| [`ddraw_backend.c`](../../src/msvcrt/ddraw_backend.c) | One-time backend init (`ddraw_ensure_backend()`), pixel-format mapping (`ddraw_bpp_to_format()`), primary/backbuffer surface allocation helpers, `ddraw_set_cooperative_level()` with fullscreen mode switching via `rb_window_set_fullscreen()`, `ddraw_set_display_mode()`, `ddraw_create_flip_chain_surface()`/`ddraw_create_regular_surface()` backend allocation. |

**Shared state:** `ddraw_priv.h` declares `g_ddraw_instance`, `ddraw_vtbl`, `surface_vtbl`, `palette_vtbl`, `clipper_vtbl`, and memory helpers `ddraw_alloc_mem()`/`ddraw_free()` (use `HeapAlloc`/`HeapFree` on PE32, `malloc`/`free` on PE32+).

---

## dsound — DirectSound (Doom95 Audio)

COM-style interfaces implemented as C structs with vtables. All use `KERNEL32_STUB`
for COM methods. Private header `dsound_priv.h` declares `my_ds_t`, `my_ds_buffer_t`,
vtable externs, and default audio format constants.

**Doom95-specific context:** DOOM95 uses DirectSound for sound effects and music.
The implementation supports 22050 Hz, stereo, 16-bit PCM as defaults.

| File | Functions |
|------|-----------|
| [`dsound_interface.c`](../../src/msvcrt/dsound_interface.c) | `DirectSoundCreate`, `DirectSoundCreate8`, `DirectSoundCreateExclusive`. COM core methods on `IDirectSound`: `QueryInterface`, `AddRef`, `Release`, `CreateSoundBuffer`, `DupliacteSoundBuffer`, `GetCaps`, `GetCooperativeLevel`, `Initialize`, `Compile3DPolygonPath`, `Notify`. Backend init via `dsound_ensure_backend()` which calls `rb_init()` and `rb_audio_open()`. Default format: 22050 Hz, 2 channels, 16-bit PCM, 4096 sample buffer. |
| [`dsound_buffer.c`](../../src/msvcrt/dsound_buffer.c) | COM methods on `IDirectSoundBuffer`: `QueryInterface`, `AddRef`, `Release`, `GetCaps`, `GetFormat`, `GetVolume`, `SetVolume`, `GetPan`, `SetPan`, `GetFrequency`, `SetFrequency`, `GetStatus`, `Reset`. Also implements the `IDirectSoundNotify` interface. |
| [`dsound_buffer_create.c`](../../src/msvcrt/dsound_buffer_create.c) | `dsound_buffer_create()` (the `CreateSoundBuffer` vtable method). Distinguishes primary buffer (sets format only) from secondary buffers (allocates `rb_audio_buf_t`, configures volume/pan/frequency controls). Validates legacy buffer descriptor size. |
| [`dsound_buffer_control.c`](../../src/msvcrt/dsound_buffer_control.c) | Runtime buffer control: `GetCurrentPosition`, `GetStatus`, `Lock`, `Unlock`, `Play`, `SetCurrentPosition`, `SetFormat`, `SetVolume`, `SetPan`, `SetFrequency`, `Stop`. Delegates to `rb_audio_buffer_*` backend calls. `Lock`/`Unlock` support two-part (discontinuous) buffer access. Volume in DSDBVOLUME range (-10000 to 0), pan in DSBPAN range (-10000 to 10000). |

**Shared state:** `dsound_priv.h` declares `dsound_vtbl`, `dsound_buffer_vtbl`, constants (`DSOUND_DEFAULT_SAMPLE_RATE=22050`, etc.), `dsound_default_wave_format()`, `dsound_apply_format()`, `dsound_ensure_backend()`, `dsound_unlink_buffer()`, `dsound_destroy_buffer()`, and memory helpers.

---

## Other Libraries

### gdi32_doom95

| File | Functions |
|------|-----------|
| [`gdi32_doom95.c`](../../src/msvcrt/gdi32_doom95.c) | `CreateDCA`, `CreateDIBitmap`, `CreateFontA`, `CreateFontIndirectA`, `CreatePen`, `CreatePenIndirect`, `CreateSolidBrush`, `DeleteObject`, `SelectObject`, `GetObjectA`, `CreateCompatibleDC`, `CreateCompatibleBitmap`, `SetTextColor`, `SetBkColor`, `SetBkMode`, `SetROP2`, `SetTextAlign`, `SetMapMode`, `GetTextMetricsA`, `GetDeviceCaps`, `GetStockObject`, `BitBlt`, `StretchBlt`, `LineTo`, `MoveToEx`, `TextOutA`, `DrawTextA`, `CreateBitmap`, `CreateBitmapIndirect`, `SetPixel`, `GetPixel`, `DeleteDC`, `CreateBitmapHandle`, `BitBltHandle`, `GetStockObjectHandle`, `ExtCreatePen`, `GetStockObjectHandle`. Doom95-specific GDI stubs — most operations return allocated handle stubs. `GetDeviceCaps` handles `HORZRES` (8), `VERTRES` (10), `BITSPIXEL` (12), `NUMCOLORS` (24) via backend display query. |

### winmm_doom95

| File | Functions |
|------|-----------|
| [`winmm_doom95.c`](../../src/msvcrt/winmm_doom95.c) | Extensive multimedia support: `timeGetTime`, `timeSetEvent`, `timeKillEvent`, `timeBeginPeriod`, `timeEndPeriod`, `joyGetNumDevs`, `joyGetPos`, `joyGetPosEx`, `joyGetDevCapsA`, `joySetCapture`, `joyReleaseCapture`, `midiOutOpen`, `midiOutClose`, `midiOutShortMsg`, `midiOutLongMsg`, `midiOutPrepareHeader`, `midiOutUnprepareHeader`, `midiOutGetDevCapsA`, `midiOutGetErrorTextA`, `midiOutReset`, `midiStreamOpen`, `midiStreamClose`, `midiStreamOut`, `midiStreamPause`, `midiStreamRestart`, `midiStreamStop`, `midiStreamPosition`, `midiStreamProperty`, `waveOutOpen`, `waveOutClose`, `waveOutWrite`, `waveOutPrepareHeader`, `waveOutUnprepareHeader`, `waveOutPause`, `waveOutRestart`, `waveOutReset`, `waveOutGetDevCapsA`, `waveOutGetErrorTextA`, `waveOutGetPitch`, `waveOutGetPlaybackRate`, `waveOutSetPlaybackRate`, `auxOutGetDevCapsA`, `mixerOpen`, `mixerClose`, `mixerGetDevCapsA`, `mixerGetControlDetails`, `mixerGetLineInfo`, `mixerGetID`, `mixerMessage`. Joystick functions delegate to `rb_joy_*` backend. MIDI functions use `winmm_doom95_midi_backend.c` for FluidSynth rendering. |
| [`winmm_doom95_midi_backend.c`](../../src/msvcrt/winmm_doom95_midi_backend.c) | `winmm_doom95_midi_backend_has_device()`, `winmm_doom95_midi_backend_open()`, `winmm_doom95_midi_backend_close()`, `winmm_doom95_midi_backend_play_event()`, `winmm_doom95_midi_backend_reset()`. FluidSynth-based MIDI rendering: finds soundfont via `MY_WINE_SOUNDFONT` env var or system paths (`/usr/share/soundfonts/FluidR3_GM.sf2`). Driver selection: `MY_WINE_FLUID_DRIVER` env var or fallback through pulseaudio → pipewire → alsa. Volume from `dwVolume` parameter. |

**Shared state:** `winmm_doom95_priv.h` declares `midi_fluidsynth_backend_t` (ready, settings, synth, driver, soundfont_path, volume) and backend function declarations.

### advapi32_registry

| File | Functions |
|------|-----------|
| [`advapi32_registry.c`](../../src/msvcrt/advapi32_registry.c) | `RegOpenKeyExA`, `RegCloseKey`, `RegQueryValueExA`, `RegSetValueExA`, `RegCreateKeyExA`, `RegDeleteKeyA`, `RegDeleteValueA`, `RegEnumKeyExA`, `RegEnumValueA`. In-memory registry simulation: 64-entry value table with path+name keys. Only `HKEY_CURRENT_USER` (pseudo-handle 0x80000001) and opened key handles are recognized. Only `REG_SZ` type is supported. |

### dplay_stub

| File | Functions |
|------|-----------|
| [`dplay_stub.c`](../../src/msvcrt/dplay_stub.c) | `DPCreate` (returns `E_ACCESSDENIED`), `DirectPlayEnumerateA` (enumerates 4 providers: TCP/IP, IPX, Serial, Modem via callback). Minimal DirectPlay support — creation is denied but enumeration works for games that probe provider availability. |

---

## Handle Manager

| File | Functions |
|------|-----------|
| [`handle_manager.c`](../../src/msvcrt/handle_manager.c) | `wine_handle_manager_init()`, `wine_handle_alloc()`, `wine_handle_get()`, `wine_handle_get_type()`, `wine_handle_add_ref()`, `wine_handle_release()`, `wine_handle_free()`. Spinlock-protected handle table (`HANDLE_TABLE_SIZE` slots). Handles 0-2 reserved for STDIN/STDOUT/STDERR. Allocation scans for the first empty slot ≥ 3. |
| [`handle_manager_runtime.c`](../../src/msvcrt/handle_manager_runtime.c) | `wine_handle_manager_state()` (returns `g_handle_manager_runtime`), `wine_handle_manager_runtime_init()` (seeds slots 0-2 with stdin/stdout/stderr file descriptors). |

**Shared state:** `handle_manager_priv.h` declares `wine_handle_manager_runtime` (table + spinlock). `include/handle_manager.h` defines `HANDLE_TYPE_*` constants (FILE, EVENT, MUTEX, SEMAPHORE, THREAD, HWIN, DC, BITMAP, HRSRC, HKEY, RB_WINDOW, etc.) and the `wine_handle_t` struct (id, type, object, ref_count).

---

## CRT — C Runtime Support

| File | Functions |
|------|-----------|
| [`crt_32_stub.c`](../../src/msvcrt/crt_32_stub.c) | 32-bit standalone CRT stubs: `__getmainargs`, `__p__acmdln_func`, `__p__fmode_func`, `__p__commode_func`, `__initenv_func`, `_amsg_exit`, `_cexit`, `_m_malloc`, `_m_free`, `_m_calloc`, `_m_realloc`, `_m_memcpy`, `_m_memset`, `_m_strlen`, `_m_strcmp`, `_m_strncmp`, `_m_memcmp`, `_m_abort`, `_m_exit`, `_m_signal`, `_m_wcslen`, `_m_localeconv`, `_m_strerror`, `_m_fprintf`, `_m_fwrite`, `_m_vfprintf`, `_m_fputc`. On PE32, uses `int $0x80` syscalls; on non-PE32, uses libc. Provides globals `_fmode`, `_commode`, `__msvcrt_app_type`, `_acmdln[256]`, `_my_wine_initenv`, `__initenv`. |
| [`crt_file.c`](../../src/msvcrt/crt_file.c) | `__iob_func`, `__acrt_iob_func`, `__wine_iob_data()`. Returns the base of the `FILE` array from `g_crt.iob`. Used by the loader to patch `__acrt_iob_func` with a direct return. |
| [`crt_globals.c`](../../src/msvcrt/crt_globals.c) | Defines `g_crt` (`wine_crt_state_t`): ctor/dtor list stubs, iob (stdin/stdout/stderr FILE structs), command-line storage, environment pointer, fmode/commode/app_type, CRT context, and various import stubs. Constructor function `crt_init_self_refs()` fixes `acmdln`/`p_acmdln` self-references. Provides function wrappers `__p__acmdln_func`, `__initenv_func`, `__p__fmode_func`, `__p__commode_func` (excluded under MY_WINE32). |
| [`crt_misc.c`](../../src/msvcrt/crt_misc.c) | `___lc_codepage_func` (returns 65001/UTF-8), `___mb_cur_max_func` (returns 6), `_errno_func`, `_lock`, `_unlock`, `fputc`, `localeconv`, `strerror`, `wcslen`. Pure-C or syscall implementations — no glibc. Defines `__mb_cur_max = 6` and `g_msvcrt_errno`. |
| [`crt_offset_discovery.c`](../../src/msvcrt/crt_offset_discovery.c) | `discover_crt_offsets()`, `find_symbol_rva_from_file()`, `scan_text_for_refptrs()`. Scans `.text` for `.refptr` instruction patterns (`mov reg, [rip+disp]; mov reg, [reg]`) to discover CRT global variable offsets. Reads COFF symbol table from the PE file. MinGW-specific layout detection. |
| [`crt_refptrs.c`](../../src/msvcrt/crt_refptrs.c) | `patch_crt_refptrs()`, `apply_refptr_patch()`. Patches PE refptr entries so CRT startup code doesn't crash on two-level indirection. `refptr_mappings[]` maps ~20 CRT symbols (`__CTOR_LIST__`, `__DTOR_LIST__`, `__image_base__`, `_acmdln`, `_fmode`, etc.) to addresses in `g_crt`. Uses reentrant thunk context for page-table safety. |
| [`crt_startup.c`](../../src/msvcrt/crt_startup.c) | `__set_app_type`, `__getmainargs`, `_getcmdline`, `_amsg_exit` wrapper, `_cexit` wrapper. `_m_*` functions with `__attribute__((ms_abi))` for direct PE calls: `_m_malloc`, `_m_free`, `_m_calloc`, `_m_realloc`, `_m_memcpy`, `_m_memset`, `_m_strlen`, `_m_strcmp`, `_m_strncmp`, `_m_memcmp`, `_m_abort`, `_m_exit`, `_m_signal`, `_m_wcslen`, `_m_localeconv`, `_m_strerror`, `_m_fprintf`, `_m_fwrite`, `_m_vfprintf`, `_m_fputc`. |
| [`crt_stdio.c`](../../src/msvcrt/crt_stdio.c) | `wine_vfprintf`, `wine_fprintf`, `wine_fwrite`. Direct syscall `INLINE_SYSCALL_WRITE` for output. Validates `wine_FILE*` against `g_crt.iob` bounds. Exposes `__msvcrt_fprintf`, `__msvcrt_fwrite`, `__msvcrt_vfprintf` function pointers. |
| [`crt_stdlib.c`](../../src/msvcrt/crt_stdlib.c) | `wine__exit` (→ `INLINE_SYSCALL_EXIT`), `wine_abort`, `wine_exit`, `wine_malloc`, `wine_calloc`, `wine_free`, `wine_realloc`, `wine_memcpy`, `wine_strlen`, `wine_strncmp`, `wine_signal`, `_amsg_exit`, `_cexit`. Exposes `__msvcrt_malloc`, `__msvcrt_calloc`, `__msvcrt_free`, `__msvcrt_realloc`, `__msvcrt_memcpy`, `__msvcrt_strlen`, `__msvcrt_strncmp`, `__msvcrt_exit`, `__msvcrt__exit`, `__msvcrt_abort`, `__msvcrt_signal` function pointers. |

**Shared state:** `msvcrt_priv.h` declares all `wine_*` internal function prototypes, `__msvcrt_*` function pointer externs, `patch_crt_refptrs()`, `apply_refptr_patch()`, `discover_crt_offsets()`, `find_symbol_rva_from_file()`, `scan_text_for_refptrs()`, and `__mb_cur_max`. `g_crt` defined in `crt_globals.c`.

---

## Resource Loading

| File | Functions |
|------|-----------|
| [`resource_win32.c`](../../src/msvcrt/resource_win32.c) | `wine_resource_find()`, `wine_resource_size()`, `wine_resource_lock()`. Parses PE resource directory tree (IMAGE_RESOURCE_DIRECTORY with named/ID entries). Allocates `wine_resource_handle` structs registered in the handle manager as `HANDLE_TYPE_HRSRC`. Lookup by module base → resource directory → type/name. |

**Shared state:** `resource_win32.h` declares the public API. Uses `include/handle_manager.h` for handle allocation.

---

## Launcher Stubs

| File | Functions |
|------|-----------|
| [`launcher_kernel32_stubs.c`](../../src/msvcrt/launcher_kernel32_stubs.c) | `GetSystemTimeAsFileTime`, `GetTickCount`, `GetSystemTime`, `SetSystemTime`, `GetLocalTime`, `SetLocalTime`, `FileTimeToSystemTime`, `FileTimeToLocalFileTime`, `SystemTimeToFileTime`, `LocalFileTimeToFileTime`, `GetSystemTimeAsFileTime`, `FindFirstFileA` (wildcard), `FindNextFileA`, `FindClose`, `GetFileAttributesA`. Launcher-specific file system and time helpers. `FindFirstFileA` supports `*` and `?` wildcard patterns via `wildcard_match_ci()`. |
| [`launcher_ui_stubs.c`](../../src/msvcrt/launcher_ui_stubs.c) | `InitCommonControls`, `PropertySheetA`, `GetSystemMenu`, `AppendMenuA`, `DialogBoxParamA`, `EndDialog`, `CreateDialogParamA`, `IsDialogMessageA`, `GetDlgItem`, `SetDlgItemTextA`, `SendDlgItemMessageA`, `CheckDlgButton`, `IsDlgButtonChecked`, `GetWindowTextA`, `SetWindowTextA`, `GetDlgItemTextA`, `GetClientRect`, `GetWindowRect`, `GetDC`, `ReleaseDC`, `InvalidateRect`, `UpdateWindow`, `DestroyWindow`, `SendMessageA`, `MapWindowPoints`, `SetCursor`, `ShowWindow`, `GetWindowLongA`, `SetWindowLongA`, `SetTimer`, `KillTimer`. Launcher UI support — thin wrappers over the user32/dialog implementations. `DialogBoxParamA` calls `CreateDialogParamA` + `user32_dialog_run_modal()`. |

---

## Calling Convention Summary

| Macro | ABI | Used By |
|-------|-----|---------|
| `KERNEL32_STUB` | MS-ABI (x86_64) / stdcall (i386) | All guest-facing exports (kernel32, user32, ddraw, dsound, gdi32, winmm, etc.) |
| `WINE_STUB` | MS-ABI (x86_64) / default (i386) | CRT functions called directly from PE (`__iob_func`, `_amsg_exit`, etc.) |
| `HANDLER` | System V ABI (both architectures) | ntdll syscall handlers called from dispatcher thunks |

On x86_64, `KERNEL32_STUB` = `WINE_STUB` = `__attribute__((ms_abi, used))`.
On i386, `KERNEL32_STUB` uses stdcall (`__attribute__((stdcall, used))`) and
`WINE_STUB` = `__attribute__((used))`.

`WINE_STUB_STATIC` adds `static` for functions that must use the MS-ABI but are
only referenced within a single translation unit (e.g. `wine_vfprintf` in `crt_stdio.c`).

---

## Cross-Cutting Dependencies

| From | Depends On |
|------|------------|
| `ntdll_*` | `include/ntdll.h`, `handle_manager`, `syscall/abi_wrappers.h` |
| `kernel32_*` | `ntdll_priv.h` (for handle helpers), `msvcrt_priv.h`, `include/kernel32.h` |
| `user32_*` | `handle_manager`, `backend/sdl2` (weak refs), `resource_win32` |
| `ddraw_*` | `handle_manager`, `backend/sdl2` (weak refs), `user32_priv.h` |
| `dsound_*` | `backend/sdl2` (weak refs), `kernel32_priv.h` |
| `gdi32_doom95.c` | `handle_manager`, `backend/sdl2` |
| `winmm_doom95.c` | `handle_manager`, `backend/sdl2`, `winmm_doom95_midi_backend.c` (FluidSynth) |
| `crt_*` | `include/crt.h`, `loader/loader_state.h` |

All backend symbols (`rb_*`, `rb_window_*`, `rb_surface_*`, `rb_audio_*`, etc.)
are declared `__attribute__((weak))` so the stubs compile even when the SDL2
backend isn't linked. Runtime checks guard against NULL function pointers.
