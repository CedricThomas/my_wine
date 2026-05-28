# SDL2 Backend — Reference

The SDL2 backend (`src/backend/sdl2/`) provides the rendering and audio subsystem
for DOOM95. It implements DirectDraw surfaces (double-buffered rendering with palette
support) and DirectSound (multi-buffer PCM playback with volume/pan/frequency control)
on top of SDL2. This is an optional dependency — the loader builds without SDL2,
and `ddraw_*`/`dsound_*` stubs in `src/msvcrt/` use `__attribute__((weak))` linkage
so they compile to no-ops when the backend isn't present.

---

## Overview

```
src/backend/sdl2/
├── rb_sdl2_priv.h              # Private structs, helper declarations, host-context macros
│
├── rb_init.c                   # SDL2 init/shutdown, display-size query
├── rb_driver_policy.c          # Video/audio driver selection and fallback
├── rb_runtime_state.c          # Init flag, signal handlers, shutdown/X11 notifications
│
├── rb_window.c                 # Window lifecycle (create, destroy, show, position, etc.)
├── rb_window_host.c            # Host-stack SDL window helpers (all SDL window calls)
├── rb_window_state.c           # Window ID refresh, cursor, surface detach/rebind
│
├── rb_surface.c                # Surface create/destroy/lock/unlock/blt/flip/desc
├── rb_surface_present.c        # Window-surface presentation + flip-chain diagnostics
│
├── rb_palette.c                # Palette create/destroy, color get/set
│
├── rb_audio.c                  # Audio device open/close, buffer create/destroy/control
├── rb_audio_mix.c              # Per-buffer mixing into the SDL stream callback
│
├── rb_event.c                  # Top-level SDL-event → rb_msg_t translation dispatch
├── rb_event_keyboard.c         # Key event translation, VK→scancode, key-watch queue
├── rb_event_window_mouse.c     # Mouse + window-event translation (WM_MOUSEMOVE, etc.)
├── rb_event_focus.c            # Focus policy, ALT-key state, synthetic focus messages
├── rb_event_queue.c            # SDL polling, wait/peek, synthetic queue plumbing
├── rb_event_state.c            # HWND↔SDL-window routing table, synthetic msg queue
├── rb_event_shutdown.c         # Shutdown sequencing (WM_CLOSE per window → WM_QUIT)
│
└── rb_input.c                  # Timer, joystick, keyboard async state, cursor
```

All `.c` files include `rb_sdl2_priv.h` as their single private header. The public
API surface is `include/render_backend.h`, which declares every backend
function using opaque `uintptr_t` handle types.

---

## Architecture: Guest vs Host Context

The most critical design constraint of the SDL2 backend is that **all SDL2 and glibc
calls must happen on the host stack with the host segment selector**.

When guest code (e.g., DOOM95) calls a Windows API stub like `LockSurface` or
`GetAsyncKeyState`, the call path looks like:

```
Guest code (DOOM95.EXE)
  → ddraw_stub (src/msvcrt/ddraw_*.c)
  → rb_surface_lock() (src/backend/sdl2/rb_surface.c)
  → rb_call_on_host_stack() (inline asm)
  → SDL function (on host stack, host GS/FS)
```

After the loader jumps to guest code, GS (64-bit) / FS (32-bit) points at the emulated
TEB. SDL and glibc (via `malloc`, `getenv`, etc.) expect the host segment selector.

### `rb_call_on_host_stack()`

Defined as a static inline in `rb_sdl2_priv.h`. Does three things atomically:

1. **Restores GS/FS** to the host selector (saved during loader setup)
2. **Switches RSP/ESP** to the UNIX stack (`unix_stack_ptr_val`)
3. **Calls the target function** via `call *fn`

On x86_64, uses naked inline assembly with explicit register clobbering (`rcx`,
`rdx`, `rsi`, `r8`-`r11`). On i386, pushes callee-saved registers (`ebp`, `ebx`,
`esi`, `edi`) around the call. Every SDL2 function, glibc `malloc`/`free`, and
`getenv` goes through this mechanism.

### `-fno-stack-protector` on backend files

Backend `.c` files that use `rb_call_on_host_stack` are compiled with
`-fno-stack-protector`. The GCC stack protector places a canary on the current
stack frame; when `rb_call_on_host_stack` switches to the host stack and calls back
into host functions (like `malloc`), the canary value is read from the *guest* stack,
causing a false-positive abort. In the current Makefile, the backend files that need
host-stack callbacks are built with `SPECIAL_CFLAGS` plus `$(SDL2_CFLAGS)`, which
includes `-fno-stack-protector`.

**Why `-mstackrealign`, not `-mno-sse`?** Unlike the stub files (which are called
directly from guest code and must disable SSE because guest registers may hold SSE
state), backend files are called from stubs that have already clobbered/restored SSE
registers. The backend files *may* call back into host functions (`malloc`, `SDL_*`)
that assume an aligned stack, so `-mstackrealign` is used instead.

### Host memory helpers

The backend provides thin wrappers that route `malloc`/`calloc`/`free`/`getenv`
through `rb_call_on_host_stack`:

| Function | Purpose |
|----------|---------|
| `rb_host_malloc(size)` | `malloc()` on host stack |
| `rb_host_calloc(nmemb, size)` | `calloc()` on host stack |
| `rb_host_free(ptr)` | `free()` on host stack |
| `rb_host_getenv(name)` | `getenv()` on host stack |

All backend-owned objects (`rb_window`, `rb_surface`, `rb_palette`, `rb_audio_buf`,
`rb_cursor`) are allocated via `rb_host_malloc` and freed via `rb_host_free`.

---

## DirectDraw Backend

Implements surface-based rendering for DOOM95's 8-bit palette display mode
(typically 320×200 at 8bpp). The backend manages flip-chain double-buffering,
palette operations, and blitting.

### Surface Lifecycle

**`rb_surface_create(w, h, format, palette, flags)`**

Allocates a pixel buffer and creates an SDL `SDL_Surface` via `SDL_CreateRGBSurfaceFrom`.

| Format | BPP | Pixel Layout |
|--------|-----|-------------|
| `RB_FORMAT_8BIT` | 8 | Palette indices |
| `RB_FORMAT_15BIT` | 15 | XRGB1555 (masks queried from SDL at runtime) |
| `RB_FORMAT_16BIT` | 16 | RGB565 (hardcoded masks: 0xF800/0x07E0/0x001F) |
| `RB_FORMAT_32BIT` | 32 | RGBA8888 (hardcoded masks: 0xFF000000/0x00FF0000/0x0000FF00/0x000000FF) |

On x86_64, the pixel buffer is first attempted via `mmap(MAP_32BIT)` to keep it
within 32-bit address space (required for guest code that uses 32-bit pointers into
the buffer). Falls back to `rb_host_malloc()` if `MAP_32BIT` fails. The flag
`s->own_buf_low32` tracks which allocation method was used for correct cleanup.

**`rb_surface_create_flip_chain(win, w, h, format, palette, backbuffer_count)`**

Creates a primary surface (flagged `RB_SURFACE_PRIMARY | RB_SURFACE_FLIP`) and,
if `backbuffer_count > 0`, a backbuffer surface (flagged `RB_SURFACE_BACK`). The
window (`rb_window`) stores handles to both surfaces and owns the backbuffer handle.

**`rb_surface_destroy(surf)`**

Unlinks from the parent window, frees the SDL surface and pixel buffer, and releases
the handle. If destroying the primary surface, also destroys the window's backbuffer.

### Surface Operations

| Function | Purpose |
|----------|---------|
| `rb_surface_lock(surf, rect, out_data, out_pitch)` | Returns a pointer into the surface's pixel buffer. Supports sub-rect offset. |
| `rb_surface_unlock(surf)` | Marks surface dirty (no real unlock — SDL surfaces are always locked). |
| `rb_surface_blt(dst, dst_rect, src, src_rect, color, flags)` | `RB_BLT_COLORFILL` → `SDL_FillRect`; `RB_BLT_SRCCOPY` → `SDL_BlitSurface` or `SDL_BlitScaled` |
| `rb_surface_flip(surf)` | Presents the frame (pumps events, calls `rb_surface_present_window`). Swaps primary/backbuffer pixel buffers on flip-chain surfaces. |
| `rb_surface_get_desc(surf, ...)` | Returns width, height, format, pitch from the SDL surface. |

### Presentation (`rb_surface_present_window`)

In `rb_surface_present.c`:

- For flip-chain surfaces (primary + backbuffer): copies backbuffer pixels to the
  window surface via `SDL_BlitSurface`/`SDL_BlitScaled` (with format conversion if
  needed), then **swaps** the primary and backbuffer pixel buffers in place. This
  gives DOOM95's next frame a fresh backbuffer while the primary now shows the
  backbuffer's content.
- For primary-only surfaces: copies the primary surface directly to the window.
- At power-of-2 flip counts (1, 2, 4, 8, 16, ...), emits a debug log with surface
  dimensions, format names, palette color counts, and a pixel-content sample
  (first nonzero byte, palette entry RGB values).

```
Backbuffer flip flow:
  1. SDL_BlitSurface(backbuffer → window surface)
  2. SDL_UpdateWindowSurface(window)
  3. Swap(primary.pixels, backbuffer.pixels)
  4. Swap(primary.own_buf, backbuffer.own_buf)
  5. Swap(primary.palette, backbuffer.palette)
  6. Apply palette to both surfaces
```

### Palette

**`rb_palette_create(num_colors)`**

Allocates `SDL_Palette` via `SDL_AllocPalette(n)`. Registered as `HANDLE_TYPE_RB_PALETTE`
in the handle manager.

**`rb_palette_set_colors(pal, start, count, colors)`**

Converts from DOOM95's `0x00BBGGRR` format to `SDL_Color` arrays, then calls
`SDL_SetPaletteColors()`.

**`rb_surface_set_palette(surf, pal)`**

Binds the palette to the surface via `SDL_SetSurfacePalette()`.

### Window

**`rb_window_create(title, x, y, w, h, flags)`**

Creates an `SDL_Window` via `rb_window_host_create()`, wraps it in an `rb_window`
struct (allocated via `rb_host_malloc`), registers it as `HANDLE_TYPE_RB_WINDOW`
in the handle manager, and sets the default cursor.

**`rb_window_set_fullscreen(win, fullscreen, w, h, bpp)`**

DOES NOT use `SDL_SetWindowFullscreen()`. Instead, creates a *new* `SDL_Window`,
destroys the old one, and updates the `rb_window` struct's `window` pointer.
This avoids Wayland fullscreen quirks. After the swap, refreshes IDs and rebinds
the guest HWND.

**Window lifecycle functions:** `show`, `minimize`, `maximize`, `restore`,
`set_position`, `set_size`, `set_title`, `get_rect`, `get_client_rect`,
`attach_guest_hwnd`, `get_dc`, `release_dc`, `set_cursor`, `warp_mouse`.

All window SDL calls go through `rb_window_host.c` helpers, which wrap each call
in `rb_call_on_host_stack()`.

### Handle Types

| Type | Constant | Used For |
|------|----------|----------|
| `HANDLE_TYPE_RB_WINDOW` | `0x63` | `rb_window` objects |
| `HANDLE_TYPE_RB_SURFACE` | `0x64` | `rb_surface` objects |
| `HANDLE_TYPE_RB_PALETTE` | `0x65` | `rb_palette` objects |
| `HANDLE_TYPE_RB_CURSOR` | `0x66` | `rb_cursor` objects |
| `HANDLE_TYPE_DD_SURFACE` | `0x41` | DDraw surface handles (msvcrt layer) |
| `HANDLE_TYPE_DD_PALETTE` | `0x61` | DDraw palette handles (msvcrt layer) |
| `HANDLE_TYPE_DS_BUFFER` | `0x51` | DirectSound buffer objects |

---

## DirectSound Backend

Implements multi-buffer audio playback with per-buffer volume, pan, and frequency
control. All mixing happens in an SDL callback registered with `SDL_OpenAudioDevice()`.

### Audio Device Lifecycle

**`rb_audio_open(sample_rate, channels, bits_per_sample, buffer_size)`**

Opens the SDL audio device with the requested format. The device callback is
`rb_audio_callback()`. Default format: 22050 Hz, 2 channels, 16-bit PCM, 4096
sample buffer.

**`rb_audio_close()`**

Stops all buffers, pauses the device, and calls `SDL_CloseAudioDevice()`.

### Buffer Management

**`rb_audio_buffer_create(format, buffer_size)`**

Allocates `rb_audio_buf` with `rb_host_malloc`, allocates a `calloc`'d data buffer,
and appends it to `g_audio.buffers` (linked list) under the device lock. Registered
as `HANDLE_TYPE_DS_BUFFER` in the handle manager.

The format defaults to the device format if any field is zero. Validates:
- `bits_per_sample` must be 8 or 16
- `channels` > 0 and `bits_per_sample` > 0

**`rb_audio_buffer_destroy(buf)`**

Stops the buffer, removes it from the linked list under the device lock, frees
the data buffer and struct, releases the handle.

### Playback Control

| Function | Purpose |
|----------|---------|
| `rb_audio_buffer_lock(buf, offset, bytes, out_ptr, out_len)` | Returns pointer into the buffer's data array. Clamps `bytes` to remaining buffer space. |
| `rb_audio_buffer_unlock(buf, ptr, len)` | No-op (no real lock needed — device lock guards concurrent access). |
| `rb_audio_buffer_play(buf, loop)` | Sets `playing = 1`, resets cursor to 0 if at end. |
| `rb_audio_buffer_stop(buf)` | Sets `playing = 0`. |
| `rb_audio_buffer_set_volume(buf, volume)` | Volume in DSDBVOLUME range (-10000 to 0). Converts to linear gain via `10^(volume/6000)`. |
| `rb_audio_buffer_set_pan(buf, pan)` | Pan in DSBPAN range (-10000 to 10000). Converts to per-channel `pan_left`/`pan_right` floats. |
| `rb_audio_buffer_set_frequency(buf, freq)` | Sets the buffer's playback frequency for sample-rate conversion. |
| `rb_audio_buffer_set_position(buf, byte_offset)` | Sets the playback cursor (converted to frame index × 2^32 fixed-point). |
| `rb_audio_buffer_get_position(buf, out_byte_offset)` | Reads the cursor and converts back to byte offset. |
| `rb_audio_buffer_is_playing(buf)` | Returns 1 if actively playing (not stopped, not at end without loop). |

### Mixing (`rb_audio_mix.c`)

The SDL callback `rb_audio_callback(stream, len)` in `rb_audio.c` zeroes the stream
and iterates over `g_audio.buffers`, calling `rb_audio_mix_buffer()` for each.

`rb_audio_mix_buffer()` implements sample-rate conversion via fixed-point cursor
arithmetic:

```
step_fp = (source_frequency << 32) / device_sample_rate
cursor_fp += step_fp for each output frame
source_index = cursor_fp >> 32
```

This gives precise fractional sample positioning. When `source_index` reaches the
end of the buffer, the buffer either loops (wraps cursor) or stops (sets `playing = 0`).

**Frame decode:**
- For 8-bit sources: converts to 16-bit via `((sample - 128) << 8)`
- For 16-bit sources: reads `int16_t` directly
- Mono sources are duplicated to both channels

**Mixing:**
- For 8-bit device output: mixes into `uint8_t` stream (center 128)
- For 16-bit device output: mixes into `int16_t` stream (center 0)
- Applies `gain × pan_left`/`pan_right` per channel
- Clamps to prevent overflow/underflow

---

## Event System

Translates SDL events into Windows-compatible `rb_msg_t` messages for the
message loop. The event system supports both blocking (`rb_event_wait`) and
non-blocking (`rb_event_peek`) polling.

### Message Structure

```c
typedef struct {
    uintptr_t hwnd;
    uint32_t  message;   // WM_* constant
    uintptr_t wParam;
    intptr_t  lParam;
    uint32_t  time;
    int32_t   pt_x, pt_y;
} rb_msg_t;
```

### Translation Pipeline

```
SDL_Event (from SDL_PeepEvents/SDL_WaitEvent)
  → rb_event_translate_sdl_event() (dispatch in rb_event.c)
  → rb_event_translate_keyboard_or_text() (rb_event_keyboard.c)
  → rb_event_translate_window_or_mouse() (rb_event_window_mouse.c)
  → rb_msg_t (returned to user32_message.c)
```

**Key translation:**
- `SDL_KEYDOWN`/`SDL_KEYUP` → `WM_KEYDOWN`/`WM_KEYUP` (or `WM_SYSKEYDOWN`/`WM_SYSKEYUP`
  if Alt is held)
- `SDL_TEXTINPUT` → `WM_CHAR`
- `SDL_MOUSEMOTION` → `WM_MOUSEMOVE`
- `SDL_MOUSEBUTTONDOWN`/`UP` → `WM_LBUTTONDOWN`/`UP`, `WM_RBUTTONDOWN`/`UP`, `WM_MBUTTONDOWN`/`UP`
- `SDL_MOUSEWHEEL` → `WM_MOUSEWHEEL` (`0x020A`)
- `SDL_WINDOWEVENT_EXPOSED` → `WM_PAINT`
- `SDL_WINDOWEVENT_SIZE_CHANGED`/`RESIZED` → `WM_SIZE`
- `SDL_WINDOWEVENT_MOVED` → `WM_MOVE`
- `SDL_WINDOWEVENT_CLOSE` → `WM_CLOSE`
- `SDL_WINDOWEVENT_FOCUS_GAINED` → `WM_ACTIVATE` + `WM_SETFOCUS`
- `SDL_WINDOWEVENT_FOCUS_LOST` → `WM_ACTIVATE` + `WM_KILLFOCUS`
- `SDL_QUIT` → `WM_QUIT`
- Alt+F4 → `WM_CLOSE`

### Key Watch Queue

The `rb_event_watch()` callback (registered via `SDL_AddEventWatch`) intercepts
key events *before* the main event queue processes them. It pushes synthetic
messages into the synthetic queue and records the event in a 64-entry watch
queue. When `rb_event_translate_key_event()` later processes the same event from
the SDL queue, `rb_event_take_watched_key()` detects the duplicate and discards it.
This prevents double-delivery of key events to the guest.

### HWND Routing

`rb_event_state.c` maintains a growable routing table (`g_window_routes[]`) that
maps between:
- Guest `HWND` (uintptr_t)
- Backend `rb_window_t` handle
- SDL `uint32_t` window ID
- Native X11 window ID (for bad-window detection)

Three lookup directions are supported:
- `rb_event_bind_window(hwnd, win)` — register a new route
- `rb_event_resolve_hwnd_from_sdl_window(window_id)` — find HWND from SDL ID
- `rb_event_resolve_hwnd_from_native_window(native_id)` — find HWND from X11 window ID
- `rb_event_unbind_window(hwnd)` — remove route (also clears active window if matched)

### Synthetic Message Queue

Events that need to be queued outside the SDL event loop (focus transitions,
shutdown sequencing, bad-window notifications) use `rb_event_push_synthetic()`.
Both `rb_event_wait()` and `rb_event_peek()` check the synthetic queue first
before polling SDL.

### Shutdown Sequencing

`rb_event_begin_shutdown()` collects all live window HWNDs and queues `WM_CLOSE`
for each, followed by `WM_QUIT`. `rb_event_translate_shutdown()` yields one
`rb_msg_t` per call until all windows are closed and `WM_QUIT` is emitted.

### X11 Bad Window Handler

`rb_init.c` installs an X11 error handler via `XSetErrorHandler` that intercepts
`BadWindow` errors (error code 3). The resource ID is stored atomically and consumed
by `rb_event_translate_bad_window()` to generate a `WM_CLOSE` for the affected window.

---

## Input

### Keyboard (`rb_input.c`)

**`rb_keyboard_get_async_state(vk)`**

Implements `GetAsyncKeyState` semantics:
- Returns `(pressed ? 0x8000 : 0) | (was_just_pressed ? 0x0001 : 0)`
- For mouse button VKs (0x01-0x06), queries `SDL_GetMouseState()`
- For keyboard VKs, queries `SDL_GetKeyboardState()` after `SDL_PumpEvents()`
- Generic VKs (VK_SHIFT=0x10, VK_CONTROL=0x11, VK_MENU=0x12) check both left and
  right variants
- `g_async_key_pressed[]` flag is set by `rb_keyboard_note_key_event()` on each
  non-repeat key-down event, and cleared after each `GetAsyncKeyState` call

**VK → Scancode mapping** is a static array `g_vk_to_scancode[256]` in `rb_sdl2_priv.h`.
Reverse lookup (`scancode_to_vk`) scans the array linearly.

### Joystick

`rb_joy_count()` returns `SDL_NumJoysticks()`. `rb_joy_get_caps()` and
`rb_joy_get_state()` open/close the joystick handle per-call. Axis values are
normalized to 0-65535 (from SDL's -32767 to 32767 range).

### Timer

`rb_timer_get_ticks()` → `SDL_GetTicks()`. `rb_timer_delay(ms)` → `SDL_Delay(ms)`.

### Cursor

`rb_cursor_create(idc)` maps Windows `IDC_*` numeric IDs (32512-32650) to
`SDL_SystemCursor` types. `rb_window_set_cursor()` applies the cursor via
`SDL_SetCursor()`.

---

## Driver Policy

**Video driver selection** (`rb_driver_policy.c`):
1. Tries `SDL_VIDEODRIVER` from environment (or default/auto)
2. If failure and no explicit driver requested: tries Wayland, then falls back to
   X11 (if `DISPLAY` is set)
3. Clears `DESKTOP_STARTUP_ID` and `XDG_ACTIVATION_TOKEN` to avoid busy cursor

**Audio driver selection:**
1. Tries `SDL_AUDIODRIVER` from environment
2. If failure and no explicit driver: tries preferred order:
   `pipewire → pulseaudio → alsa → jack → sndio → dsp → dummy → disk`
3. Then tries all available drivers in order (excluding `disk` and `dummy` as last resort)

---

## Build Configuration

### Detection

```makefile
SDL2_CFLAGS := $(shell pkg-config --cflags sdl2 2>/dev/null || echo "-I/usr/include/SDL2")
SDL2_LIBS   := $(shell pkg-config --libs sdl2 2>/dev/null || echo "-lSDL2")
```

For 32-bit builds, a link test verifies that the 32-bit SDL2 library is available:

```makefile
SDL2_LIBS_32 := $(shell echo 'int main(void){return 0;}' | $(CC) -m32 -x c - \
    -o /tmp/__sdl2_32_test $(SDL2_LIBS) -lm 2>/dev/null && \
    echo "$(SDL2_LIBS) -lm" && rm -f /tmp/__sdl2_32_test || echo "")
```

If `SDL2_LIBS_32` is empty, the 32-bit backend objects and tests are excluded
from the build.

### Compilation Flags

| Flag | Purpose |
|------|---------|
| `-mstackrealign` | Ensures 16-byte stack alignment (backend may call host functions that need SSE) |
| `-fno-stack-protector` | Prevents false canary corruption from `rb_call_on_host_stack` stack switching |
| `$(filter-out -mno-sse,$(CFLAGS))` | Removes `-mno-sse` (applied to stubs for guest safety) — backend needs SSE for host functions |

The Makefile has explicit `CFLAGS_backend/sdl2/*.o` rules for files that use
`rb_call_on_host_stack` (the bulk of the backend).

### Build Output

| Binary | Backend Objects | Path |
|--------|----------------|------|
| `my_wine64` | `$(BACKEND_OBJS)` | `build/backend/*.o` |
| `my_wine32` | `$(BACKEND32_OBJS)` (if `SDL2_LIBS_32` non-empty) | `build32/backend/*.o` |

Both binaries link with `$(SDL2_LIBS)` (or `$(SDL2_LIBS_32)` for 32-bit).

### Source Discovery

```makefile
BACKEND_SRC = $(sort $(shell find src/backend -name '*.c' 2>/dev/null))
BACKEND_OBJS = $(patsubst src/backend/%.c,$(BUILDDIR)/backend/%.o,$(BACKEND_SRC))
BACKEND32_OBJS = $(if $(SDL2_LIBS_32),$(patsubst src/backend/%.c,$(BUILDDIR32)/backend/%.o,$(BACKEND_SRC)))
```

The `find` command discovers all `.c` files in `src/backend/`, making the build
system self-adding for new backend files.

---

## Integration

### With msvcrt Stubs

The `ddraw_*` and `dsound_*` stubs in `src/msvcrt/` delegate to the SDL2 backend:

| Stub File | Backend Calls |
|-----------|---------------|
| `ddraw_backend.c` | `rb_init()`, `rb_window_create()`, `rb_window_set_fullscreen()`, `rb_surface_create_flip_chain()`, `rb_surface_create()`, `rb_display_get_size()` |
| `ddraw_surface_ops.c` | `rb_surface_lock()`, `rb_surface_unlock()`, `rb_surface_blt()`, `rb_surface_flip()`, `rb_surface_get_desc()`, `rb_surface_set_palette()` |
| `ddraw_palette.c` | `rb_palette_create()`, `rb_palette_set_colors()` |
| `ddraw_mode.c` | `rb_display_get_size()` |
| `dsound_interface.c` | `rb_init()`, `rb_audio_open()` |
| `dsound_buffer_create.c` | `rb_audio_buffer_create()` |
| `dsound_buffer_control.c` | `rb_audio_buffer_lock()`, `rb_audio_buffer_unlock()`, `rb_audio_buffer_play()`, `rb_audio_buffer_stop()`, `rb_audio_buffer_set_volume()`, `rb_audio_buffer_set_pan()`, `rb_audio_buffer_set_frequency()`, `rb_audio_buffer_set_position()`, `rb_audio_buffer_get_position()`, `rb_audio_buffer_is_playing()` |
| `dsound_buffer.c` | `rb_audio_buffer_destroy()` |
| `user32_window.c` | `rb_window_create()`, `rb_window_attach_guest_hwnd()` |
| `user32_window_ops.c` | `rb_window_destroy()`, `rb_window_show()`, `rb_window_minimize()`, `rb_window_maximize()`, `rb_window_restore()`, `rb_window_set_position()`, `rb_window_set_size()`, `rb_window_set_title()`, `rb_window_get_rect()`, `rb_window_get_client_rect()` |
| `user32_message.c` | `rb_event_wait()`, `rb_event_peek()`, `rb_event_push()` |
| `user32_input.c` | `rb_keyboard_get_async_state()`, `rb_cursor_create()`, `rb_window_set_cursor()`, `rb_window_warp_mouse()` |
| `gdi32_doom95.c` | `rb_display_get_size()` (for `GetDeviceCaps`) |
| `winmm_doom95.c` | `rb_timer_get_ticks()`, `rb_timer_delay()`, `rb_joy_count()`, `rb_joy_get_caps()`, `rb_joy_get_state()` |

All backend symbols are declared `__attribute__((weak))` in `render_backend.h`
declarations (via the include chain). When the backend isn't linked, stubs check
for NULL function pointers and return safe defaults.

### With user32

The window and event systems are tightly coupled with `user32_*` code:
- `user32_window_lifecycle.c` calls `rb_window_create()` during `CreateWindowExA`
- `user32_focus.c` coordinates with `rb_event_activate_window()` and `rb_event_set_active_window()`
- `user32_window.c` calls `rb_window_attach_guest_hwnd()` to bind the guest HWND
- `rb_window_rebind_guest()` calls back into `user32_activate_window_direct()` (weak ref)

### With winmm_doom95

Focus tracking in `rb_event_focus.c` calls `winmm_doom95_set_application_active()`
(weak ref) when focus gains/losses, enabling the MIDI backend to pause/resume
audio processing.

### With gdi32_doom95

`GetDeviceCaps(HORZRES/VERTRES)` and `GetSystemMetrics(SM_CXSCREEN/SM_CYSCREEN)`
query `rb_display_get_size()` for the current display resolution.

---

## Init/Shutdown Sequence

```
Launch
  → user32_ensure_backend() → ddraw_ensure_backend() / dsound_ensure_backend()
  → rb_init()
      → rb_backend_capture_requested_drivers() (reads SDL_VIDEODRIVER, SDL_AUDIODRIVER)
      → rb_backend_init_sdl_call() (on host stack)
          → rb_backend_init_video() (SDL_Init with Wayland→X11 fallback)
          → rb_backend_init_audio() (driver preference order)
      → rb_backend_capture_active_drivers_call() (logs active drivers)
      → rb_runtime_install_signal_handlers() (SIGINT, SIGTERM → shutdown flag)
      → rb_event_install_watch() (SDL_AddEventWatch for key dedup)
      → rb_runtime_set_initialized(1)

Runtime
  → rb_init() is idempotent (returns 0 if already initialized)
  → rb_event_maybe_pump_host() throttled at min_interval_ms (used during flip)

Shutdown (SIGINT/SIGTERM or guest ExitProcess)
  → rb_runtime_note_shutdown_request() sets g_shutdown_requested = 1
  → rb_event_begin_shutdown() queues WM_CLOSE for all windows + WM_QUIT
  → Message loop delivers WM_CLOSE to each window (window destroys its surfaces)
  → Message loop delivers WM_QUIT (loop exits)
  → rb_shutdown()
      → rb_audio_close() (stops all buffers, closes device)
      → rb_call_on_host_stack(SDL_Quit)
      → rb_runtime_restore_signal_handlers()
      → rb_runtime_set_initialized(0)
```

---

## Global State

| Symbol | Type | Owner |
|--------|------|-------|
| `g_audio` | `rb_audio_state` | `rb_init.c` |
| `g_backend_initialized` | `int` | `rb_runtime_state.c` |
| `g_shutdown_requested` | `volatile sig_atomic_t` | `rb_runtime_state.c` |
| `g_prev_sigint_action` / `g_prev_sigterm_action` | `struct sigaction` | `rb_runtime_state.c` |
| `g_async_key_down[256]` | `uint8_t` | `rb_input.c` |
| `g_async_key_pressed[256]` | `uint8_t` | `rb_input.c` |
| `g_active_window` | `uintptr_t` | `rb_event_state.c` |
| `g_window_routes[]` | `rb_window_route *` | `rb_event_state.c` |
| `g_synthetic_queue[]` | `rb_msg_t *` | `rb_event_state.c` |
| `g_key_watch_queue[64]` | `rb_key_watch_event` | `rb_event_keyboard.c` |
| `g_alt_key_down` | `int` | `rb_event_focus.c` |
| `g_x11_bad_window_pending` | `uintptr_t` (atomic) | `rb_runtime_state.c` |
| `g_shutdown_hwnds[]` | `uintptr_t *` | `rb_event_shutdown.c` |
| `g_prev_x_error_handler` | `XErrorCallback` | `rb_init.c` |

---

## Cross-Reference Summary

| Concept | Primary File(s) |
|---------|----------------|
| SDL2 init/shutdown | `rb_init.c`, `rb_driver_policy.c` |
| Runtime state (signals, init flag) | `rb_runtime_state.c` |
| Host context switching | `rb_sdl2_priv.h` (inline functions) |
| Window lifecycle | `rb_window.c`, `rb_window_host.c`, `rb_window_state.c` |
| Surface creation/operations | `rb_surface.c` |
| Surface presentation | `rb_surface_present.c` |
| Palette management | `rb_palette.c` |
| Audio device | `rb_audio.c` |
| Audio mixing | `rb_audio_mix.c` |
| Event dispatch | `rb_event.c` |
| Keyboard translation | `rb_event_keyboard.c` |
| Mouse/window translation | `rb_event_window_mouse.c` |
| Focus policy | `rb_event_focus.c` |
| Event queue/polling | `rb_event_queue.c` |
| HWND routing | `rb_event_state.c` |
| Shutdown sequencing | `rb_event_shutdown.c` |
| Timer, joystick, cursor | `rb_input.c` |
| VK ↔ Scancode mapping | `rb_sdl2_priv.h` |
| Public API | `include/render_backend.h` |
