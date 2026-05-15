# Subplan 2: SDL2 Backend

Implement `src/backend/sdl2/*.c` — the SDL2 implementation of `render_backend.h`.

## Dependencies
- **Subplan 1 complete** — `render_backend.h` must exist
- **SDL2 installed** — `sudo apt install libsdl2-dev`

## Checklist

### 2.1 Window Lifecycle
- [x] `rb_init()` → `SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_EVENTS)`
- [x] `rb_shutdown()` → `SDL_Quit()`
- [x] `rb_window_create()` → `SDL_CreateWindow()` + allocate handle from handle manager
- [x] `rb_window_destroy()` → `SDL_DestroyWindow()` + free handle
- [x] `rb_window_show()` → `SDL_ShowWindow()` / `SDL_HideWindow()`
- [x] `rb_window_set_position()` → `SDL_SetWindowPosition()`
- [x] `rb_window_set_size()` → `SDL_SetWindowSize()`
- [x] `rb_window_set_title()` → `SDL_SetWindowTitle()`
- [x] `rb_window_get_rect()` → `SDL_GetWindowPosition()` + `SDL_GetWindowSize()`
- [x] `rb_window_get_client_rect()` → `SDL_GetWindowSize()`
- [x] `rb_window_set_fullscreen()` → `SDL_SetWindowFullscreen()` (create new window if dims change)
- [x] `rb_window_get_dc()` → return mock HDC wrapping `SDL_GetWindowSurface()`
- [x] `rb_window_release_dc()` → no-op, return 1
- [x] `rb_window_set_cursor()` → `SDL_SetWindowCursor()` (or `SDL_SetCursor()`)
- [x] `rb_window_warp_mouse()` → `SDL_WarpMouseInWindow()`

### 2.2 Surface Lifecycle
- [x] `rb_surface_create()` → `SDL_CreateRGBSurfaceWithFormatFrom()` with **exact pitch** (no padding)
  - For 8-bit: pitch = width (e.g., 320 bytes/row for 320x200)
  - Allocate with `malloc`, wrap in `SDL_CreateRGBSurfaceFrom()`
- [x] `rb_surface_create_flip_chain()` → create primary surface bound to window + backbuffer surface
- [x] `rb_surface_destroy()` → `SDL_FreeSurface()` + `free()` underlying buffer
- [x] `rb_surface_lock()` → return pointer to `SDL_Surface->pixels` + `SDL_Surface->pitch`
- [x] `rb_surface_unlock()` → mark surface dirty (flag for `SDL_UpdateWindowSurface`)
- [x] `rb_surface_blt()` → `SDL_BlitSurface()` or `SDL_SoftStretch()` (for scaling)
  - Handle `RB_BLT_SRCCOPY` (simple copy) and `RB_BLT_COLORFILL` (fill rect)
- [x] `rb_surface_flip()` → `SDL_UpdateWindowSurface()` or `SDL_UpdateWindowSurfaceRects()`
- [x] `rb_surface_get_desc()` → return width, height, format, pitch from `SDL_Surface`
- [x] `rb_surface_set_palette()` → `SDL_SetSurfacePalette()` + `SDL_SetPaletteColors()`

### 2.3 Palette
- [x] `rb_palette_create()` → `SDL_CreatePalette()`
- [x] `rb_palette_destroy()` → `SDL_FreePalette()`
- [x] `rb_palette_set_colors()` → `SDL_SetPaletteColors()` (convert 0x00BBGGRR → `SDL_Color`)
- [x] `rb_palette_get_colors()` → `SDL_GetPaletteColors()` (convert `SDL_Color` → 0x00BBGGRR)

### 2.4 Audio
- [x] `rb_audio_open()` → `SDL_OpenAudioDevice()` with 22050 Hz, 16-bit stereo
- [x] `rb_audio_close()` → `SDL_CloseAudioDevice()`
- [x] `rb_audio_buffer_create()` → allocate PCM buffer + metadata (format, play state, volume, pan, frequency)
- [x] `rb_audio_buffer_destroy()` → free buffer
- [x] `rb_audio_buffer_lock()` → return pointer to PCM buffer + available length
- [x] `rb_audio_buffer_unlock()` → mark buffer ready for mixing
- [x] `rb_audio_buffer_play()` → set PLAYING flag + start mixing (loop if `D` flag set)
- [x] `rb_audio_buffer_stop()` → clear PLAYING flag
- [x] `rb_audio_buffer_set_volume()` → store gain (convert -10000..0 → 0.0..1.0)
- [x] `rb_audio_buffer_set_pan()` → store L/R scale (convert -10000..10000 → left/right 0.0..1.0)
- [x] `rb_audio_buffer_set_frequency()` → store target sample rate (flag resampling if differs from master)

### 2.5 Event System
- [x] `rb_event_wait()` → `SDL_WaitEvent()` + translate to `rb_msg_t`
- [x] `rb_event_peek()` → `SDL_PeepEvents(..., SDL_GETEVENT)` + translate
- [x] `rb_event_push()` → translate `rb_msg_t` → `SDL_Event` + `SDL_PushEvent()`

**SDL Event → Windows MSG translation**:
| SDL Event | Windows MSG |
|-----------|-------------|
| `SDL_KEYDOWN` | `WM_KEYDOWN` (0x0100) |
| `SDL_KEYUP` | `WM_KEYUP` (0x0101) |
| `SDL_MOUSEMOTION` | `WM_MOUSEMOVE` (0x0200) |
| `SDL_MOUSEBUTTONDOWN` | `WM_LBUTTONDOWN` (0x0201) / `WM_RBUTTONDOWN` (0x0204) |
| `SDL_MOUSEBUTTONUP` | `WM_LBUTTONUP` (0x0202) / `WM_RBUTTONUP` (0x0205) |
| `SDL_WINDOWEVENT` (resize) | `WM_SIZE` (0x0005) |
| `SDL_WINDOWEVENT` (move) | `WM_MOVE` (0x0003) |
| `SDL_WINDOWEVENT` (close) | `WM_CLOSE` (0x0010) → also `PostQuitMessage(0)` |
| `SDL_WINDOWEVENT` (minimize) | `WM_SYSCOMMAND` (0x0112) with `SC_MINIMIZE` |
| `SDL_QUIT` | `WM_QUIT` (0x0012) |

### 2.6 Input
- [x] `rb_timer_get_ticks()` → `SDL_GetTicks()`
- [x] `rb_timer_delay()` → `SDL_Delay()`
- [x] `rb_joy_count()` → `SDL_NumJoysticks()`
- [x] `rb_joy_get_caps()` → `SDL_JoystickOpen()` → query axes/buttons/name → `SDL_JoystickClose()`
  - Map axis range: SDL -32768..32767 → WINMM 0..65535
- [x] `rb_joy_get_state()` → `SDL_JoystickGetAxis()` + `SDL_JoystickGetButton()`
- [x] `rb_keyboard_get_async_state()` → `SDL_GetKeyboardState()` + VK→Scancode map
  - Return `SHORT`: top bit = pressed, high bit = toggle state
- [x] `rb_cursor_create()` → `SDL_CreateSystemCursor()` (map IDC_ARROW→0, IDC_CROSS→1, etc.)
- [x] `rb_cursor_destroy()` → `SDL_FreeCursor()`
- [x] `rb_cursor_show()` → `SDL_ShowCursor()`

### 2.7 Build
- [x] Edit `Makefile`: add `-lSDL2 -lSDL2main` to LDFLAGS
- [x] Edit `Makefile`: add `src/backend/` to source file discovery

### 2.8 Test
- [x] `tests/test_sdl2_backend.c` — comprehensive test covering init, window, surface, palette, blt, audio, event, joystick, keyboard, cursor, cleanup (all pass)

---

## Files
| File | Action |
|------|--------|
| `include/render_backend.h` | **New** (~160 lines) |
| `src/backend/sdl2/rb_sdl2_priv.h` | **New** (~130 lines) |
| `src/backend/sdl2/rb_init.c` | **New** (~50 lines) |
| `src/backend/sdl2/rb_window.c` | **New** (~200 lines) |
| `src/backend/sdl2/rb_surface.c` | **New** (~250 lines) |
| `src/backend/sdl2/rb_palette.c` | **New** (~100 lines) |
| `src/backend/sdl2/rb_audio.c` | **New** (~380 lines) |
| `src/backend/sdl2/rb_event.c` | **New** (~250 lines) |
| `src/backend/sdl2/rb_input.c` | **New** (~150 lines) |
| `tests/test_sdl2_backend.c` | **New** (~200 lines) |
| `Makefile` | Edit: add SDL2 backend build rules + test target |

**~1,870 lines, 5 phases, 13 tasks — COMPLETE**
