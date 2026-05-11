# DOOM95 on my_wine — Scope and Architecture Definition

## 1. Stub Count by Complexity

### Trivial Stubs (return constant/NULL, 1–2 lines) — 23 functions

These return fixed values. No state, no SDL calls.

| # | Function | DLL | Stub Implementation |
|---|----------|-----|-------------------|
| 1 | `AdjustWindowRect` | user32 | `*rect = rect_original; return TRUE;` (SDL manages frame size) |
| 2 | `AdjustWindowRectEx` | user32 | Same as above |
| 3 | `CallNextHookEx` | user32 | `return 0;` (no hook chain) |
| 4 | `DeleteDC` | gdi32 | Return `TRUE` (no-op; HDC not reference-counted in SDL) |
| 5 | `EnableWindow` | user32 | `return TRUE;` (irrelevant under SDL) |
| 6 | `GetActiveWindow` | user32 | Return current main window `HWND` |
| 7 | `GetDesktopWindow` | user32 | Return sentinel `HWND` (`(HWND)1`) |
| 8 | `GetFocus` | user32 | Return current main window `HWND` |
| 9 | `GetStockObject` | gdi32 | Return sentinel handle based on index (WHITEBRUSH=1, BLACKPEN=2…) |
| 10 | `InvalidateRect` | user32 | `return TRUE;` |
| 11 | `IsWindow` | user32 | Return `TRUE` if handle exists in window table |
| 12 | `LoadIconA` | user32 | Return sentinel `HICON` (`(HICON)0xBAD00001`) |
| 13 | `MapWindowPoints` | user32 | Identity transform; return `nCount` |
| 14 | `SetFocus` | user32 | Return previous `HWND` (tracked in a single global) |
| 15 | `UpdateWindow` | user32 | `return TRUE;` |
| 16 | `ValidateRect` | user32 | `return TRUE;` |
| 17 | `SetBkColor` | gdi32 | Store color in HDC struct; return previous |
| 18 | `SetTextColor` | gdi32 | Store color in HDC struct; return previous |
| 19 | `UnrealizeObject` | gdi32 | `return TRUE;` |
| 20 | `ClipCursor` | user32 | `return TRUE;` (no SDL equivalent) |
| 21 | `BeginPaint` | user32 | Return mock `HDC` (same as `GetDC`) |
| 22 | `EndPaint` | user32 | `return TRUE;` |
| 23 | `DeviceIoControl` | kernel32 | `return FALSE;` |

### Simple Wrappers (direct SDL2/C stdlib mapping, 5–15 lines) — 35 functions

| # | Function | DLL | Mapping |
|---|----------|-----|---------|
| 1 | `GetTickCount` | kernel32 | `SDL_GetTicks()` |
| 2 | `timeGetTime` | winmm | `SDL_GetTicks()` |
| 3 | `Sleep` | kernel32 | Already implemented; no new code |
| 4 | `wsprintfA` | user32 | `vsprintf()` wrapper (translate `%%` → `%`) |
| 5 | `SetRect` | user32 | `r->left=x; r->top=y; r->right=r; r->bottom=b;` |
| 6 | `GetAsyncKeyState` | user32 | `SDL_GetKeyboardState()` with VK→Scancode map |
| 7 | `joyGetNumDevs` | winmm | `SDL_NumJoysticks()` |
| 8 | `joyGetDevCapsA` | winmm | `SDL_JoystickOpen()` → query caps → close |
| 9 | `joyGetPosEx` | winmm | `SDL_JoystickGetAxis()/GetButton()` → fill `JOYINFOEX` |
| 10 | `GetSystemMetrics` | user32 | Map index to `SDL_GetCurrentDisplayMode()` or hardcoded value |
| 11 | `GetModuleFileNameA` | kernel32 | Return pre-stored DOOM95.EXE path |
| 12 | `GetVersion` | kernel32 | `return 0x80000005;` (Windows 2000) |
| 13 | `GetCommandLineA` | kernel32 | Already implemented; no new code |
| 14 | `GetEnvironmentStrings` | kernel32 | Return `environ` pointer |
| 15 | `GetCPInfo` | kernel32 | Fill with CP-1252 info; `return TRUE;` |
| 16 | `GetStdHandle` | kernel32 | Already implemented; no new code |
| 17 | `SetStdHandle` | kernel32 | `return TRUE;` |
| 18 | `GetConsoleMode` | kernel32 | `return FALSE;` |
| 19 | `SetConsoleMode` | kernel32 | `return FALSE;` |
| 20 | `ReadConsoleInputA` | kernel32 | `return FALSE;` |
| 21 | `WriteConsoleA` | kernel32 | `printf()` or `return FALSE;` |
| 22 | `GetCurrentProcessId` | kernel32 | `return getpid();` |
| 23 | `GetCurrentThread` | kernel32 | `return (HANDLE)(uintptr_t)-2;` |
| 24 | `GetFileType` | kernel32 | `return FILE_TYPE_DISK;` |
| 25 | `GetTimeZoneInformation` | kernel32 | Return `TIME_ZONE_ID_UNKNOWN` with zeroed bias |
| 26 | `DosDateTimeToFileTime` | kernel32 | DOS datetime → FILETIME conversion (calendar math) |
| 27 | `FileTimeToDosDateTime` | kernel32 | FILETIME → DOS datetime (reverse) |
| 28 | `FileTimeToLocalFileTime` | kernel32 | Identity copy (timezone offset = 0) |
| 29 | `LocalFileTimeToFileTime` | kernel32 | Identity copy |
| 30 | `GetFileTime` | kernel32 | `stat()` → FILETIME or `return FALSE` |
| 31 | `GetLastWriteTime` | kernel32 | (alias of GetFileTime) |
| 32 | `GetSystemInfo` | kernel32 | `SDL_CPUCount()` + hardcoded x86 fields |
| 33 | `GetModuleHandleA` | kernel32 | Already implemented; no new code |
| 34 | `FreeLibrary` | kernel32 | Already implemented; no new code |
| 35 | `SearchPathA` | kernel32 | `strchr()` / path copy; return resolved path |

### Moderate Implementations (state management, 15–50 lines) — 50 functions

These require handle tables, message queues, or internal state.

#### Window Management (10 functions)
| Function | Implementation |
|----------|---------------|
| `CreateWindowExA` | `SDL_CreateWindow()` + allocate `HWND` from handle table + store style/params |
| `DestroyWindow` | `SDL_DestroyWindow()` + free `HWND` |
| `ShowWindow` | Map `nCmdShow` to `SDL_ShowWindow()`/`SDL_HideWindow()`/`SDL_RestoreWindow()` |
| `SetWindowPos` | `SDL_SetWindowPosition()` + `SDL_SetWindowSize()` |
| `MoveWindow` | Same as `SetWindowPos` |
| `SetWindowTextA` | `SDL_SetWindowTitle()` |
| `GetWindowRect` | `SDL_GetWindowPosition()` + `SDL_GetWindowSize()` → fill `RECT` |
| `GetClientRect` | `SDL_GetWindowSize()` → fill `RECT` with `(0,0,w,h)` |
| `RegisterClassA` | Store `WNDCLASSA` + `WNDPROC` in internal hash (keyed by class name) |
| `GetWindowLongA` / `SetWindowLongA` | Internal `HWND` → `LONG[]` map; `GWL_WNDPROC` tracks callback |

#### Message Loop (8 functions)
| Function | Implementation |
|----------|---------------|
| `GetMessageA` | `SDL_WaitEvent()` → translate `SDL_Event` to `MSG` |
| `PeekMessageA` | `SDL_PeepEvents(..., SDL_GETEVENT)` → translate |
| `DispatchMessageA` | Call stored `WNDPROC` for target window |
| `PostMessageA` | Translate `MSG` → `SDL_Event`, `SDL_PushEvent()` |
| `PostQuitMessage` | `SDL_PushEvent(SDL_QUIT)` |
| `SendMessageA` | Direct `WNDPROC` call; special cases for `WM_GETTEXT`, `WM_SETTEXT`, `WM_GETCLIENTAREA` |
| `DefWindowProcA` | Stub: `return 0` |
| `CallWindowProcA` | Direct function pointer call |

#### Keyboard / Cursor (4 functions)
| Function | Implementation |
|----------|---------------|
| `LoadCursorA` | Map `IDC_ARROW`/`IDC_CROSS` → `SDL_CreateSystemCursor()` |
| `SetCursor` | `SDL_SetCursor()` with stored `SDL_Cursor*` |
| `SetCursorPos` | `SDL_WarpMouseInWindow()` |
| `LoadStringA` | Internal string table lookup (from PE `.rsrc`) |

#### GDI Surface (10 functions)
| Function | Implementation |
|----------|---------------|
| `CreateDCA` | Return mock `HDC` wrapping `SDL_GetWindowSurface()` |
| `GetDC` | Same as `CreateDCA` (mock `HDC`) |
| `ReleaseDC` | `return 1;` (no-op) |
| `CreateDIBitmap` | `SDL_CreateRGBSurface()` from `BITMAPINFOHEADER` |
| `StretchDIBits` | `SDL_BlitSurface()` / `SDL_SoftStretch()` — **critical for GDI fallback** |
| `GetObjectA` | Return stored metrics from `HBITMAP`/`HFONT` |
| `DeleteObject` | Type-tagged: `SDL_FreeSurface()` for bitmaps, `SDL_FreePalette()` for palettes, `free()` for fonts |
| `GetDeviceCaps` | Map index to hardcoded/display values |
| `CreateFontA` | Store font metrics (height) in `HFONT`; return sentinel |
| `GetWindowRect` (GDI) | (shared with user32) |

#### Memory / File / Thread (12 functions)
| Function | Implementation |
|----------|---------------|
| `GlobalAlloc` | `malloc()` (ignore flags) |
| `LocalAlloc` | `malloc()` (ignore flags) |
| `LocalFree` | `free()` |
| `CreateFileA` | `fopen()` → wrap `FILE*` in `HANDLE` from handle table |
| `CloseHandle` | Already implemented; extend to handle `HWND`/`HDC`/GDI handles |
| `ReadFile` | Already implemented; may need handle-table extension |
| `WriteFile` | Already implemented; may need handle-table extension |
| `SetFilePointer` | `fseek()` on wrapped `FILE*` |
| `GetFileSize` | `fseek(0,SEEK_END)` + `ftell()` |
| `GetFileAttributesA` | `stat()` → `FILE_ATTRIBUTE_*` bits |
| `FindNextFileA` | `readdir()` wrapped in `HANDLE` |
| `CreateThread` | `SDL_CreateThread()` → wrap in `HANDLE` |
| `ExitThread` | `SDL_ExitThread()` |
| `GetCurrentThreadId` | `SDL_ThreadID()` |
| `CreateDirectoryA` | `mkdir()` |
| `DeleteFileA` | `remove()` |

#### Dialog System (7 functions)
| Function | Implementation |
|----------|---------------|
| `CreateDialogParamA` | Parse dialog resource from PE `.rsrc`; store control state; return mock `HWND` |
| `IsDialogMessageA` | Route messages to dialog `WNDPROC` if active |
| `GetDlgItem` | Lookup control from dialog + ID |
| `CheckDlgButton` | Store checkbox state per control |
| `IsDlgButtonChecked` | Return checkbox state |
| `SetDlgItemTextA` | Set text label per control |
| `MessageBoxA` | `SDL_ShowSimpleMessageBox()` |

### Complex Subsystems (50+ lines, multi-file) — 7 subsystems

#### 1. IDirectDraw Mock Vtable (~18 methods)
**File**: `src/stubs/ddraw_interface.c`, `src/stubs/ddraw_surface.c`
**What**: A complete `IDirectDraw` interface struct of function pointers that DOOM95 calls via vtable. Each method wraps SDL2 rendering.
**Methods**:
- `QueryInterface` (stub: return E_NOTIMPL)
- `AddRef` / `Release` (stub: refcounting)
- `Compact` (stub: DD_OK)
- `GetCapabilities` — fill `DDCAPS` (primary, flip, blit)
- `GetAvailableVidMem` — stub: return fake value
- `GetMonitorFrequency` — stub: return 60
- `GetFourCCCodes` — stub: return NULL
- `GetSurfaceFromDC` — stub: return DDERR_UNSUPPORTED
- `RestoreDisplayMode` — stub: DD_OK
- `SetCooperativeLevel` — **critical** — parse `DDSCL_EXCLUSIVE|DDSCL_FULLSCREEN` flag
- `SetDisplayMode` — **critical** — `SDL_CreateWindow()` with new dims or `SDL_SetWindowFullscreen()`
- `CreateSurface` — **critical** — `SDL_CreateRGBSurface()` (8-bit palleted for game surfaces)
- `CreateSurfaceFromBitmap` — stub: DD_OK
- `DuplicateSurface` — stub: return DDERR_UNSUPPORTED
- `EnumDisplayModes` — stub: DD_OK
- `GetDisplayMode` — return current dims/bpp
- `GetFlipStatus` — stub: DD_OK
- `GetGDIEvent` — stub: return NULL
- `WaitForVerticalBlank` — stub: DD_OK

**IDirectDrawSurface vtable** (~24 methods, same pattern):
- `AddAttachedSurface`, `DeleteAttachedSurface`, `GetAttachedSurface` — stub
- `AddOverlayDirtyRect` — stub: DD_OK
- `Blt` — **critical** — `SDL_BlitSurface()` with rect mapping
- `BltBatch` — stub: DD_OK
- `BltFast` — `SDL_BlitSurface()` (simplified, no effects)
- `GetBltStatus` — stub: DD_OK
- `GetDC` / `ReleaseDC` — return mock `HDC`
- `GetFlipStatus` — stub: DD_OK
- `GetOverlayPosition` / `SetOverlayPosition` — stub: DD_OK
- `GetSurfaceDesc` — **critical** — return `lPitch` + `lpSurface` (raw pixel ptr)
- `IsLost` — stub: DD_OK
- `Lock` — **critical** — return raw pixel ptr + pitch
- `Restore` — stub: DD_OK (no mode switch under SDL)
- `SetClipper` / `GetClipper` — stub: DD_OK
- `SetPalette` — **critical** — `SDL_SetPaletteColors()`
- `Unlock` — **critical** — mark surface dirty (for `SDL_UpdateWindowSurface`)
- `OverrideCursor` — stub: DD_OK

#### 2. IDirectSound Mock Vtable (~12 methods)
**File**: `src/stubs/dsound_interface.c`, `src/stubs/dsound_buffer.c`
**What**: `IDirectSound` interface + `IDirectSoundBuffer` per-buffer vtable. Custom PCM mixer.

**IDirectSound methods**:
- `QueryInterface`, `AddRef`, `Release` — stub
- `CreateSoundBuffer` — **critical** — allocate `IDirectSoundBuffer` + PCM buffer
- `GetCaps` — fill `DSCAPS`
- `Initialize` — stub: DS_OK
- `Compact` — stub: DS_OK
- `DuplicateSoundBuffer` — stub: DS_OK
- `Get CooperativeLevel` — **critical** — store `DSSCL_PRIORITY`
- `SetCooperativeLevel` — **critical** — store level
- `GetSpeakerConfig` / `SetSpeakerConfig` — stub
- `OpenDevice` / `CloseDevice` — stub
- `EnumerateDevCaps` — stub
- `EnumerateDevices` — stub

**IDirectSoundBuffer methods** (~12):
- `GetCaps` — fill `DSBCAPS`
- `GetCurrentPosition` — stub: return 0
- `GetFormat` — return stored `WAVEFORMATEX`
- `GetVolume` / `SetVolume` — store volume (scaled in mixer)
- `Lock` / `Unlock` — **critical** — return PCM buffer ptr
- `Play` — **critical** — flag buffer for mixing
- `SetFrequency` — **critical** — store rate (flag resampling)
- `SetPan` — store pan (applied in mixer)
- `SetStatus` — **critical** — PLAYING/STOPPED flag
- `Stop` — unflag buffer
- `Restore` — stub: DS_OK
- `SetLoopPoints` — store loop points

**PCM Mixer** (custom, ~100 lines):
```
Each frame:
  for each PLAYING buffer:
    resample if needed (buffer rate → master rate)
    apply volume gain
    apply pan (L/R scale)
    mix into master buffer
  SDL_QueueAudio(master buffer)
```

#### 3. Dialog System (~120 lines, 2 files)
**Files**: `src/stubs/user32_dialog.c`, `include/dialog_types.h`
**What**: Parse PE dialog resources (`.rsrc` section), build in-memory control tree, store state.
DOOM95 has 3 dialog templates (IDs 104, 130, 131) + 7 bitmap resources.
Controls include: buttons, checkboxes, radio buttons, edit boxes, static text, list boxes.
Rendering is done by Windows GDI — we provide mock handles and `DefWindowProcA`-style behavior.

#### 4. In-Memory Registry (~100 lines)
**File**: `src/stubs/advapi32_registry.c`
**What**: Flat hashmap (`path/key` → `HKEY`) with value storage.
DOOM95 reads: install path, last resolution, saved options.
Writes: settings changes.
`HKEY` values are `uintptr_t` pointers into the hash.

#### 5. TLS Subsystem (~80 lines)
**File**: `src/stubs/kernel32_extended.c`
**What**: 2D array `tls[slot][thread_id]`. `TlsAlloc` assigns next free slot.
`TlsGetValue` is already implemented; need `TlsAlloc`, `TlsFree`, `TlsSetValue`.

#### 6. Message Dispatch (~150 lines)
**Files**: `src/stubs/user32_message.c`, `include/user32_types.h`
**What**: `SDL_Event` ↔ `MSG` translation + `WNDPROC` table per `HWND` + `SendMessageA` direct dispatch.
Critical: `TranslateMessage` (WM_KEYUP synthesis) is not imported but may be needed.

#### 7. DirectPlay Stub (~50 lines)
**File**: `src/stubs/dplay_stub.c`
**What**: `DPCreate` (ordinal #1) returns mock `IDirectPlay` with no-op methods.
Single-player only — game degrades gracefully.

---

### Summary Counts

| Category | Count | Lines |
|----------|-------|-------|
| Trivial Stubs | 23 | ~45 |
| Simple Wrappers | 35 | ~350 |
| Moderate Implementations | 50 | ~1,200 |
| IDirectDraw vtable | 18 + 24 = 42 | ~800 |
| IDirectSound vtable | 12 + 12 = 24 | ~600 |
| Dialog System | 7 + resource parser | ~120 |
| Registry | 5 | ~100 |
| TLS | 3 | ~80 |
| Message Dispatch | 8 | ~150 |
| DirectPlay | 1 | ~50 |
| **Total unique functions** | **188** | **~3,515** |
| **New SDL2-specific code** | | **~2,800** |
| **Type/header definitions** | | **~500** |
| **Import table entries** | **164** (matching imports.md) | |

Note: 188 unique stubs across all DLLs + vtables; some functions already exist in my_wine (listed above as "already implemented") and are reused without modification.

---

## 2. Abstraction Layer Interface

The `render_backend.h` sits between Windows stubs and SDL2. All stubs call into this interface — never directly into SDL2.

```c
/*
 * render_backend.h
 *
 * Abstraction between Windows API stubs and the rendering backend (SDL2).
 * This file defines the interface that user32.c, gdi32.c, ddraw.c, dsound.c
 * all call into.  A different backend can be swapped by providing a different
 * .c implementation of these functions.
 */

#ifndef RENDER_BACKEND_H
#define RENDER_BACKEND_H

#include <stdint.h>

/* ---- Opaque handle types ---- */

/* Each handle is an opaque uintptr_t.  0 = NULL/invalid. */
typedef uintptr_t rb_window_t;       /* HWND-equivalent      */
typedef uintptr_t rb_surface_t;      /* HBITMAP / DDSurface  */
typedef uintptr_t rb_palette_t;      /* HPALETTE / DDPalette */
typedef uintptr_t rb_audio_buf_t;    /* DirectSound buffer   */
typedef uintptr_t rb_dc_t;           /* HDC-equivalent       */
typedef uintptr_t rb_cursor_t;       /* HCURSOR-equivalent   */
typedef uintptr_t rb_font_t;         /* HFONT-equivalent     */

/* ---- Pixel format ---- */

typedef enum {
    RB_FORMAT_8BIT  = 8,
    RB_FORMAT_15BIT = 15,
    RB_FORMAT_16BIT = 16,
    RB_FORMAT_32BIT = 32,
} rb_pixel_format_t;

/* ---- Rect ---- */

typedef struct {
    int32_t x, y, w, h;
} rb_rect_t;

/* ---- Initialization / Shutdown ---- */

/* Call once before any other backend function. */
int rb_init(void);

/* Call on exit.  Cleans up all backend resources. */
void rb_shutdown(void);

/* ---- Window Lifecycle ---- */

/*
 * Create a window.
 *   title      – window title
 *   x, y       – position (RB_HINT_AUTO for auto-placement)
 *   w, h       – client area dimensions
 *   flags      – RB_WINDOW_FULLSCREEN | RB_WINDOW_RESIZABLE | RB_WINDOW_SHOWN
 * Returns window handle, or 0 on failure.
 */
rb_window_t rb_window_create(const char *title,
                             int x, int y, int w, int h,
                             uint32_t flags);

/* Destroy a window and all associated surfaces/DCs. */
int rb_window_destroy(rb_window_t win);

/* Show or hide a window. */
int rb_window_show(rb_window_t win, int show);  /* show: 1=show, 0=hide */

/* Set window position. */
int rb_window_set_position(rb_window_t win, int x, int y);

/* Set window size (client area). */
int rb_window_set_size(rb_window_t win, int w, int h);

/* Set window title. */
int rb_window_set_title(rb_window_t win, const char *title);

/* Get window position + size → fills *rect. */
int rb_window_get_rect(rb_window_t win, rb_rect_t *rect);

/* Get client area size → fills *rect (x=y=0). */
int rb_window_get_client_rect(rb_window_t win, rb_rect_t *rect);

/* Toggle fullscreen. */
int rb_window_set_fullscreen(rb_window_t win, int fullscreen, int w, int h, int bpp);

/* ---- Surface Lifecycle ---- */

/*
 * Create a surface in system memory.
 *   w, h          – dimensions
 *   format        – pixel format
 *   palette       – associated palette (0 for none)
 *   flags         – RB_SURFACE_FLIP | RB_SURFACE_OFFSCREEN
 * Returns surface handle, or 0 on failure.
 */
rb_surface_t rb_surface_create(int w, int h, rb_pixel_format_t format,
                               rb_palette_t palette, uint32_t flags);

/* Create a flip-chain surface (primary + backbuffers). */
rb_surface_t rb_surface_create_flip_chain(rb_window_t win,
                                          int w, int h,
                                          rb_pixel_format_t format,
                                          rb_palette_t palette,
                                          int backbuffer_count);

/* Destroy a surface. */
int rb_surface_destroy(rb_surface_t surf);

/*
 * Lock a surface for direct pixel access.
 *   surf       – surface to lock
 *   rect       – region to lock (NULL = entire surface)
 *   *out_data  – filled with pointer to pixel data
 *   *out_pitch – filled with bytes-per-row
 * Returns 0 on success.
 */
int rb_surface_lock(rb_surface_t surf, const rb_rect_t *rect,
                    uint8_t **out_data, int *out_pitch);

/* Unlock after writing. */
int rb_surface_unlock(rb_surface_t surf);

/*
 * Blt: copy from src region to dst region.
 *   dst, dst_rect  – destination surface + region
 *   src, src_rect  – source surface + region (or NULL for color fill)
 *   color          – fill color (used when src == 0)
 * Returns 0 on success.
 */
int rb_surface_blt(rb_surface_t dst, const rb_rect_t *dst_rect,
                   rb_surface_t src, const rb_rect_t *src_rect,
                   uint32_t color, uint32_t flags);  /* flags: RB_BLT_COLORFILL, RB_BLT_SRCCOPY */

/* Flip front/back buffers. */
int rb_surface_flip(rb_surface_t surf);

/* Get surface description (dims, format, pitch). */
int rb_surface_get_desc(rb_surface_t surf,
                        int *w, int *h, rb_pixel_format_t *format,
                        int *pitch);

/* ---- Palette ---- */

/*
 * Create a palette.
 *   num_colors   – 16, 256, or 0 (default)
 * Returns palette handle.
 */
rb_palette_t rb_palette_create(int num_colors);

/* Destroy a palette. */
int rb_palette_destroy(rb_palette_t pal);

/* Set palette colors. */
int rb_palette_set_colors(rb_palette_t pal,
                          uint32_t start, uint32_t count,
                          const uint32_t *colors);  /* colors as 0x00BBGGRR */

/* Apply palette to a surface. */
int rb_surface_set_palette(rb_surface_t surf, rb_palette_t pal);

/* Get palette colors. */
int rb_palette_get_colors(rb_palette_t pal,
                          uint32_t start, uint32_t count,
                          uint32_t *colors);

/* ---- DC (Device Context) ---- */

/* Get a DC for a window (used for GDI operations). */
rb_dc_t rb_window_get_dc(rb_window_t win);

/* Release a DC (no-op in SDL2 backend). */
int rb_window_release_dc(rb_window_t win, rb_dc_t dc);

/* ---- Cursor ---- */

/* Create a system cursor. idc: IDC_ARROW=0, IDC_CROSS=1, etc. */
rb_cursor_t rb_cursor_create(int idc);

/* Destroy a cursor. */
int rb_cursor_destroy(rb_cursor_t cur);

/* Set cursor for a window. */
int rb_window_set_cursor(rb_window_t win, rb_cursor_t cur);

/* Show/hide cursor. */
int rb_cursor_show(int show);

/* Warp mouse to window-relative position. */
int rb_window_warp_mouse(rb_window_t win, int x, int y);

/* ---- Event System ---- */

/*
 * Poll for events.  Returns 1 if an event was read, 0 if empty.
 * On success, *out_msg is filled with a Windows MSG structure.
 */
typedef struct {
    uintptr_t hwnd;
    uint32_t  message;   /* WM_* constant */
    uint32_t  wParam;
    int32_t   lParam;
    uint32_t  time;
    int32_t   pt_x, pt_y;
} rb_msg_t;

/* Blocking wait for next message.  Returns 0 on WM_QUIT. */
int rb_event_wait(rb_msg_t *out_msg);

/* Non-blocking peek.  Returns 1 if message available, 0 if empty. */
int rb_event_peek(rb_msg_t *out_msg);

/* Push a message into the queue. */
int rb_event_push(rb_msg_t *msg);

/* ---- Audio ---- */

/* Open the audio device.  Returns 0 on success. */
int rb_audio_open(int sample_rate, int channels, int bits_per_sample,
                  int buffer_size);

/* Close the audio device. */
void rb_audio_close(void);

/* Create a sound buffer. */
rb_audio_buf_t rb_audio_buffer_create(int format, int buffer_size);

/* Destroy a sound buffer. */
int rb_audio_buffer_destroy(rb_audio_buf_t buf);

/*
 * Lock a sound buffer for writing PCM data.
 *   buf       – buffer handle
 *   offset    – byte offset into buffer
 *   bytes     – number of bytes to lock
 *   *out_ptr  – filled with pointer to writable memory
 *   *out_len  – actual bytes available (may be < bytes if wrapping)
 * Returns 0 on success.
 */
int rb_audio_buffer_lock(rb_audio_buf_t buf,
                         uint32_t offset, uint32_t bytes,
                         uint8_t **out_ptr, uint32_t *out_len);

/* Unlock after writing. */
int rb_audio_buffer_unlock(rb_audio_buf_t buf,
                           const uint8_t *ptr, uint32_t len);

/* Start playing a buffer. loop: 1=cycle. */
int rb_audio_buffer_play(rb_audio_buf_t buf, int loop);

/* Stop a buffer. */
int rb_audio_buffer_stop(rb_audio_buf_t buf);

/* Set volume.  Value: -10000 (mute) to 0 (full). */
int rb_audio_buffer_set_volume(rb_audio_buf_t buf, int volume);

/* Set pan.  Value: -10000 (full left) to 10000 (full right). */
int rb_audio_buffer_set_pan(rb_audio_buf_t buf, int pan);

/* Set playback frequency (Hz). */
int rb_audio_buffer_set_frequency(rb_audio_buf_t buf, uint32_t freq);

/* ---- Timer ---- */

/* Get elapsed time in milliseconds (since rb_init). */
uint32_t rb_timer_get_ticks(void);

/* Delay execution for ms milliseconds. */
void rb_timer_delay(uint32_t ms);

/* ---- Joystick ---- */

/* Return number of connected joysticks. */
int rb_joy_count(void);

/*
 * Get joystick capabilities.
 *   idx         – joystick index (0-based)
 *   name        – buffer for name (NULL to skip)
 *   name_len    – size of name buffer
 *   *n_axes     – filled with axis count
 *   *n_buttons  – filled with button count
 *   *min, *max  – filled with axis range
 */
int rb_joy_get_caps(int idx, char *name, int name_len,
                    int *n_axes, int *n_buttons,
                    uint16_t *min, uint16_t *max);

/*
 * Get joystick state.
 *   idx    – joystick index
 *   axes   – output array of axis values (0..65535)
 *   n_axes – length of axes array
 *   buttons – output array of button states (0 or 1)
 *   n_buttons – length of buttons array
 */
int rb_joy_get_state(int idx,
                     uint16_t *axes, int n_axes,
                     uint8_t *buttons, int n_buttons);

/* ---- Keyboard ---- */

/*
 * Get keyboard state.
 *   vk        – virtual key code (Windows VK_*)
 * Returns SHORT with top bit set if pressed, high bit for toggle state.
 */
int16_t rb_keyboard_get_async_state(int vk);

/* ---- Constants / Flags ---- */

#define RB_HINT_AUTO      -1

#define RB_WINDOW_FULLSCREEN    (1 << 0)
#define RB_WINDOW_RESIZABLE     (1 << 1)
#define RB_WINDOW_SHOWN         (1 << 2)

#define RB_SURFACE_FLIP     (1 << 0)
#define RB_SURFACE_OFFSCREEN (1 << 1)
#define RB_SURFACE_PRIMARY  (1 << 2)
#define RB_SURFACE_BACK     (1 << 3)

#define RB_BLT_SRCCOPY      (1 << 0)
#define RB_BLT_COLORFILL    (1 << 1)

#define RB_OK                0
#define RB_FAIL              -1

#endif /* RENDER_BACKEND_H */
```

---

## 3. File Layout

```
my_wine/
├── include/
│   ├── user32_types.h          # HWND, HDC, HCURSOR, MSG, RECT, WNDCLASSA, PAINTSTRUCT
│   ├── gdi32_types.h           # HBITMAP, HPALETTE, HFONT, LOGPALETTE, BITMAPINFO, COLORREF
│   ├── ddraw_types.h           # DDSURFACEDESC, DDCAPS, DDSCAPS, DDPIXELFORMAT, HRESULT codes
│   ├── dsound_types.h          # WAVEFORMATEX, DSBCAPS, DSCAPS, DSBDIRECTION, HRESULT codes
│   ├── winmm_types.h           # JOYCAPSA, JOYINFOEX, JOYINFOEX, MIDIHDR, MMRESULT codes
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
│   │                          # BeginPaint, EndPaint, MapWindowPoints,
│   │                          # GetSystemMetrics
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

### File Size Estimates

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

---

## 4. Implementation Order

Each milestone is independently testable — a sample program or specific DOOM95 behavior confirms success.

### Milestone 1: Window Creation

**Goal**: my_wine can create an SDL2 window from `CreateWindowExA`

**New files**:
- `src/stubs/user32_window.c` (partial: `CreateWindowExA`, `ShowWindow`, `SetWindowTextA`)
- `src/backend/render_backend_sdl2.c` (partial: `rb_init`, `rb_window_create`, `rb_window_show`, `rb_window_set_title`, `rb_shutdown`)
- `include/render_backend.h` (window functions only)
- `include/user32_types.h` (HWND, WNDCLASSA, RECT)

**New functions** (8):
- `CreateWindowExA`, `ShowWindow`, `SetWindowTextA`, `DestroyWindow`, `RegisterClassA`, `GetWindowLongA`, `SetWindowLongA`, `IsWindow`

**Import table entries** added: `user32.dll` → all 8 functions

**Test**: Run a minimal PE test program that calls `CreateWindowExA` → `ShowWindow` → `Sleep(2000)` → `DestroyWindow`. Confirm SDL2 window appears with correct title and dimensions.

---

### Milestone 2: Event Loop

**Goal**: my_wine can poll events via `GetMessageA`/`PeekMessageA` and dispatch to a `WNDPROC`

**New files**:
- `src/stubs/user32_message.c`
- `include/user32_types.h` (extend with MSG, WNDPROC)

**New functions** (9):
- `GetMessageA`, `PeekMessageA`, `DispatchMessageA`, `PostMessageA`, `PostQuitMessage`, `SendMessageA`, `DefWindowProcA`, `CallWindowProcA`, `SetWindowsHookExA` (stub)

**Test**: Run a PE test program with a message loop: `while (GetMessage(&msg, 0, 0, 0)) { DispatchMessage(&msg); }`. Clicking/closing the SDL2 window should trigger `WM_CLOSE` → `WM_DESTROY` → `PostQuitMessage` → loop exit.

---

### Milestone 3: DirectDraw Interface + Surfaces

**Goal**: my_wine can create an `IDirectDraw` interface, set display mode, and create surfaces

**New files**:
- `src/stubs/ddraw_interface.c`
- `src/stubs/ddraw_surface.c` (partial: `CreateSurface`, `GetSurfaceDesc`)
- `include/ddraw_types.h`
- `src/backend/render_backend_sdl2.c` (extend: surface/flip/palette)

**New functions** (18 + 24 = 42 vtable methods):
- `DirectDrawCreate` + all `IDirectDraw` vtable methods + `IDirectDrawSurface::CreateSurface`, `GetSurfaceDesc`, `Lock`, `Unlock`, `Restore`, `SetPalette`

**Test**: Run a PE test program that calls `DirectDrawCreate` → `SetCooperativeLevel` → `SetDisplayMode(320, 200, 8)` → `CreateSurface(320, 200, 8-bit)`. Confirm the surface is created with correct dimensions. Check `GetSurfaceDesc` returns valid pitch.

---

### Milestone 4: Surface Rendering (Lock → Write → Unlock → Flip)

**Goal**: my_wine can render a surface to the screen

**New files**:
- `src/stubs/ddraw_surface.c` (extend: `Blt`, `Flip`)
- `src/backend/render_backend_sdl2.c` (extend: `rb_surface_blt`, `rb_surface_flip`, palette)

**New functions** (extending existing vtable methods):
- `IDirectDrawSurface::Blt` → `SDL_BlitSurface`
- `IDirectDrawSurface::BltFast` → simplified blit
- `IDirectDrawSurface::Flip` → `SDL_UpdateWindowSurface`
- `IDirectDrawSurface::SetPalette` → `SDL_SetPaletteColors`

**Test**: Run a PE test program that:
1. Creates a flip chain (primary + 1 back buffer)
2. Locks the back buffer
3. Writes a checkerboard pattern (alternating colors)
4. Unlocks
5. Flips
Confirm the checkerboard is visible on the SDL2 window.

---

### Milestone 5: DirectSound Playback

**Goal**: my_wine can play a sound (DirectSound buffer Lock → write → Play)

**New files**:
- `src/stubs/dsound_interface.c`
- `src/stubs/dsound_buffer.c`
- `include/dsound_types.h`
- `src/backend/render_backend_sdl2.c` (extend: audio functions)

**New functions** (12 + 12 = 24 vtable methods):
- `DirectSoundCreate` + `IDirectSound` vtable + `IDirectSoundBuffer` vtable + PCM mixer

**Test**: Run a PE test program that:
1. Calls `DirectSoundCreate`
2. Creates a 1-second sine-wave buffer at 22050 Hz, 16-bit stereo
3. Locks → writes sine wave → unlocks
4. Calls `Play()`
Confirm audio is heard.

---

### Milestone 6: DOOM95 Init Completes

**Goal**: DOOM95.EXE passes the init sequence (window created, DDraw init, DSound init, MIDI init) without crashing

**New files**:
- `src/stubs/winmm_timer.c`
- `src/stubs/winmm_joystick.c`
- `src/stubs/winmm_midi.c` (stub version — `midiStreamOpen` returns success but no playback)
- `src/stubs/advapi32_registry.c`
- `src/stubs/kernel32_extended.c` (file I/O, TLS, timing)
- `src/stubs/dplay_stub.c`

**New functions**: ~40 remaining kernel32, 5 advapi32, 15 winmm, 1 dplay

**Test**: Launch DOOM95.EXE. Confirm:
- No crash during init
- Window appears (may be empty)
- Init messages/errors don't crash (registry stubs return `ERROR_SUCCESS`)
- DirectPlay stub returns `DP_OK` or graceful error
- Program reaches the title screen or crashes with a known render issue (not an init crash)

---

### Milestone 7: DOOM95 Game Loop

**Goal**: DOOM95 game loop runs (events processed, frames rendered, sounds play)

**New files**:
- `src/stubs/user32_input.c` (complete: `GetAsyncKeyState`, `GetKeyboardState` mapping)
- `src/stubs/gdi32_surface.c` (complete: `StretchDIBits`, `CreateDIBitmap`)
- `src/stubs/gdi32_palette.c` (complete)

**New functions**: ~15 (input + GDI rendering)

**Test**: Launch DOOM95.EXE from title screen. Confirm:
- Game world is rendered (even if colors are wrong)
- Player movement works (keyboard input)
- Sound effects play
- No repeated crashes
- FPS is reasonable (≥ 15 fps is acceptable for first pass)

---

### Milestone 8: DOOM95 Menus / Dialogs

**Goal**: DOOM95 options menus, episode selection, and CD key dialogs work

**New files**:
- `src/stubs/user32_dialog.c` (complete)
- `include/dialog_types.h`
- `src/stubs/gdi32_font.c`

**New functions**: 7 (dialog system + font)

**Test**: From title screen, open options menu. Confirm:
- Dialog appears (even if controls don't render perfectly)
- Checkboxes and radio buttons can be toggled
- Menu can be closed
- Episode selection works

---

### Summary of Milestones

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

---

## 5. Risks and Mitigations

### Risk 1: PE32 (32-bit) Incompatibility — **HIGH likelihood, CRITICAL impact**

**The gap**: DOOM95 is PE32 (i386). my_wine currently only supports PE32+ (x64). The import table uses `IMAGE_THUNK_DATA64`, PEB offsets are for x64, and thunks are x86_64 machine code.

**Mitigation options** (in order of preference):
1. **Shim DOOM95 through a 32-bit compatibility layer**: Add `IMAGE_THUNK_DATA32` support to the import parser. This is the minimal change — the PE32 vs PE32+ difference is mostly in thunk size (4 bytes vs 8 bytes) and header layout. The stub functions still execute as x64 on the host, so only the PE parser needs PE32 awareness. **Estimated effort: 200 lines, 2 days.**
2. **Full PE32 loader**: Add complete PE32 section mapping, 32-bit relocation handling, and 32-bit thunk generation. **Estimated effort: 1,000 lines, 1–2 weeks.**
3. **Recompile DOOM95 as x64**: Use MinGW-w64 or DOSBox-X's i386 compatibility layer to produce a PE32+ binary. This is high-risk — Watcom-compiled code has inline assembly and ABI-specific assumptions.

**Recommendation**: Option 1 — add PE32 thunk/image support to the parser only. The stub functions remain x64. The key changes are in `pe_imports.c` (read 32-bit thunks) and `teb_peb.c` (32-bit image base offsets). This is the path of least resistance.

---

### Risk 2: Watcom CRT vs mingw-w64 CRT — **MEDIUM likelihood, HIGH impact**

**The gap**: DOOM95 is compiled with Watcom C/C++ 3.1. The CRT startup sequence (`__watcom_startup` → env parsing → heap init → `D_DoomMain`) is completely different from mingw-w64's `__getmainargs` path. my_wine currently patches mingw-w64-specific `.rdata` refptrs.

**Impact**: If the CRT startup code calls into `kernel32.dll` or `msvcrt.dll` functions we haven't stubbed, it will crash before `D_DoomMain` is reached. The entry point is at `0x004444d8` → jumps to Watcom CRT → eventually calls `D_DoomMain`.

**Mitigation**:
1. **Trace the Watcom CRT init path** and stub any Windows API calls it makes (likely minimal — just heap/init).
2. **Bypass CRT entirely**: Set the entry point to `D_DoomMain` directly (RVA can be resolved from the jump table at `0x00449d6c`). This skips the CRT's env/argv parsing. The game reconstructs command-line args via `GetCommandLineA`.
3. **Pre-seed the Watcom CRT globals** (the `0x618364` pointer stored at entry) to valid values.

**Recommendation**: Option 2 — bypass Watcom CRT and jump directly to `D_DoomMain`. The game uses `GetCommandLineA` for args (which we stub) and `GlobalAlloc`/`LocalAlloc` for heap (which we map to `malloc`). This avoids CRT compatibility entirely.

---

### Risk 3: DirectDraw Surface Format Assumptions — **HIGH likelihood, MEDIUM impact**

**The gap**: DOOM95 renders at 320×200 with 8-bit (256-color) palettized surfaces. SDL2's `SDL_CreateRGBSurface()` supports 8-bit with palette, but the pitch/stride may differ from what DOOM95 expects. DDraw surfaces may assume row-aligned pitch (e.g., 320 bytes per row for 8-bit), but SDL2 may pad to a larger alignment.

**Impact**: If pitch is wrong, DOOM95's `Lock`-write-`Unlock` path will write to wrong memory offsets → visual corruption.

**Mitigation**:
1. **In `rb_surface_lock`**, return a custom-pitched buffer that matches DOOM95's expected pitch. Copy from/to the SDL surface as needed.
2. **Force SDL surface pitch** by allocating with exact pitch and zero-filling padding bytes (may require `SDL_CreateRGBSurfaceWithFormatFrom` with a manually-allocated buffer).
3. **Accept visual corruption in edge cases** and fix only when DOOM95 actually surfaces the issue.

**Recommendation**: Option 2 — allocate buffers with exact expected pitch. For 320×200 8-bit, that's 320 bytes per row, 64000 bytes total. Allocate with `malloc` and wrap in `SDL_CreateRGBSurfaceFrom()`.

---

### Risk 4: Dialog Rendering — **MEDIUM likelihood, LOW impact**

**The gap**: DOOM95 uses Windows-native GDI dialogs for menus. These dialogs rely on `DefWindowProcA`, control drawing (buttons, checkboxes, static text), and palette management. If we stub `CreateDialogParamA` to just create an empty `HWND`, the dialog won't render.

**Impact**: Game menus won't work. DOOM95 may crash trying to access dialog controls. However, the in-game HUD is rendered on the DDraw surface (not via GDI), so gameplay is unaffected.

**Mitigation**:
1. **Minimal dialog support**: Parse dialog resources, create mock controls, respond to `CheckDlgButton`/`IsDlgButtonChecked` in memory. Let `DefWindowProcA` return 0. The dialog won't visually render, but button state changes will be tracked.
2. **SDL2-based dialog rendering**: Draw controls using `SDL_BlitSurface` (text + rectangles). High effort, low payoff.
3. **Skip dialogs entirely**: Hard-code the game to skip dialog creation and use a text-mode config instead.

**Recommendation**: Option 1 — track dialog control state in memory. Dialogs won't visually render, but the game won't crash. Dialogs can be polished in a later pass.

---

### Risk 5: MIDI Playback — **MEDIUM likelihood, LOW impact**

**The gap**: DOOM95 uses `midiStreamOpen` + `midiStreamOut` for music. SDL2 has no MIDI support. Stubbing means silent music.

**Impact**: No background music. Sound effects (DirectSound) still work. Low impact on playability.

**Mitigation**:
1. **Stub (phase 1)**: `midiStreamOpen` returns a valid handle. `midiStreamOut` is a no-op. Game runs silent for music.
2. **libmodplug/Timidity++ (phase 2)**: Link one of these libraries. `midiStreamOpen` opens a synth instance. `midiStreamOut` feeds MIDI bytes to the synth.
3. **SDL_mixer (phase 2)**: `Mix_OpenAudio()` + `Mix_PlayMusic()` with MOD support.

**Recommendation**: Option 1 for initial milestone. Add Option 2 or 3 in a follow-up.

---

## 6. Effort Estimate

### Totals

| Metric | Count |
|--------|-------|
| **Total new stub functions** | 188 (164 import entries + 24 vtable-only methods) |
| **Unique implementations needed** | ~120 (after deduplication with existing my_wine stubs) |
| **Total new lines of code** | ~6,000 (5,500 implementation + 500 headers) |
| **Total new files** | 20 (16 stub files + 1 backend file + 3 new type headers) |
| **Import table additions** | 164 entries across 8 DLLs |
| **Ordinal table additions** | 1 entry (DPLAY ordinal #1) |

### Time Estimate

At **200 lines/day** (realistic for new API stubs with testing):

| Category | Lines | Days |
|----------|-------|------|
| Headers / types | 500 | 2–3 |
| render_backend_sdl2.c | 2,000 | 8–10 |
| user32 (4 files) | ~1,000 | 4–5 |
| gdi32 (3 files) | ~360 | 2 |
| ddraw (2 files) | ~850 | 4–5 |
| dsound (2 files) | ~600 | 3 |
| winmm (3 files) | ~260 | 1–2 |
| advapi32 / kernel32 / dplay | ~670 | 3–4 |
| **Subtotal** | **~6,000** | **27–34 days** |
| **Testing / integration / bugfixing** | | **+10–14 days** |
| **Total** | | **~7–10 weeks** |

### Per-Milestone Estimate

| Milestone | Lines | Duration |
|-----------|-------|----------|
| 1: Window | ~500 | 3 days |
| 2: Events | ~400 | 2 days |
| 3: DDraw Init | ~850 | 4 days |
| 4: Rendering | ~300 | 2 days |
| 5: Audio | ~600 | 3 days |
| 6: DOOM95 Init | ~700 | 4 days |
| 7: Game Loop | ~400 | 2 days |
| 8: Dialogs | ~300 | 2 days |
| **Subtotal** | **~4,050** | **22 days** |
| Integration / debugging | | **10–14 days** |
| **Total** | | **~6–8 weeks** |

---

## Appendix: PE32 Note

DOOM95.EXE is **PE32 (32-bit x86)**, not PE32+ (64-bit x86_64). This is the single largest blocker. Before any of the above milestones can run DOOM95, the my_wine loader must gain PE32 support. The minimum changes are:

1. **`pe_imports.c`**: Read `IMAGE_THUNK_DATA32` (4-byte thunks) instead of `IMAGE_THUNK_DATA64` (8-byte). Detect via `IMAGE_NT_HEADERS32` vs `IMAGE_NT_HEADERS64` magic.
2. **`teb_peb.c`**: Use 32-bit image base (`0x00400000`) and 32-bit PEB/TEB offsets.
3. **`import_resolve.c`**: Handle 32-bit thunk patching (IAT writes are 4 bytes, not 8).
4. **`trampoline.S` / `thunk_gen.c`**: The generated thunks and dispatcher can remain x64 — they execute on the host and call x64 stub functions. Only the PE image is 32-bit.

This PE32 support is a prerequisite for Milestone 6 and should be addressed before the above milestones.
