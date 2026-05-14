# Subplan 2: SDL2 Backend

Implement `src/backend/sdl2/*.c` — the SDL2 implementation of `render_backend.h`.

## Dependencies
- **Subplan 1 complete** — `render_backend.h` must exist
- **SDL2 installed** — `sudo apt install libsdl2-dev`

## Checklist

### 2.1 Window Lifecycle
- [ ] `rb_init()` → `SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_EVENTS)`
- [ ] `rb_shutdown()` → `SDL_Quit()`
- [ ] `rb_window_create()` → `SDL_CreateWindow()` + allocate handle from handle manager
- [ ] `rb_window_destroy()` → `SDL_DestroyWindow()` + free handle
- [ ] `rb_window_show()` → `SDL_ShowWindow()` / `SDL_HideWindow()`
- [ ] `rb_window_set_position()` → `SDL_SetWindowPosition()`
- [ ] `rb_window_set_size()` → `SDL_SetWindowSize()`
- [ ] `rb_window_set_title()` → `SDL_SetWindowTitle()`
- [ ] `rb_window_get_rect()` → `SDL_GetWindowPosition()` + `SDL_GetWindowSize()`
- [ ] `rb_window_get_client_rect()` → `SDL_GetWindowSize()`
- [ ] `rb_window_set_fullscreen()` → `SDL_SetWindowFullscreen()` (create new window if dims change)
- [ ] `rb_window_get_dc()` → return mock HDC wrapping `SDL_GetWindowSurface()`
- [ ] `rb_window_release_dc()` → no-op, return 1
- [ ] `rb_window_set_cursor()` → `SDL_SetWindowCursor()` (or `SDL_SetCursor()`)
- [ ] `rb_window_warp_mouse()` → `SDL_WarpMouseInWindow()`

### 2.2 Surface Lifecycle
- [ ] `rb_surface_create()` → `SDL_CreateRGBSurfaceWithFormatFrom()` with **exact pitch** (no padding)
  - For 8-bit: pitch = width (e.g., 320 bytes/row for 320x200)
  - Allocate with `malloc`, wrap in `SDL_CreateRGBSurfaceFrom()`
- [ ] `rb_surface_create_flip_chain()` → create primary surface bound to window + backbuffer surface
- [ ] `rb_surface_destroy()` → `SDL_FreeSurface()` + `free()` underlying buffer
- [ ] `rb_surface_lock()` → return pointer to `SDL_Surface->pixels` + `SDL_Surface->pitch`
- [ ] `rb_surface_unlock()` → mark surface dirty (flag for `SDL_UpdateWindowSurface`)
- [ ] `rb_surface_blt()` → `SDL_BlitSurface()` or `SDL_SoftStretch()` (for scaling)
  - Handle `RB_BLT_SRCCOPY` (simple copy) and `RB_BLT_COLORFILL` (fill rect)
- [ ] `rb_surface_flip()` → `SDL_UpdateWindowSurface()` or `SDL_UpdateWindowSurfaceRects()`
- [ ] `rb_surface_get_desc()` → return width, height, format, pitch from `SDL_Surface`
- [ ] `rb_surface_set_palette()` → `SDL_SetSurfacePalette()` + `SDL_SetPaletteColors()`

### 2.3 Palette
- [ ] `rb_palette_create()` → `SDL_CreatePalette()`
- [ ] `rb_palette_destroy()` → `SDL_FreePalette()`
- [ ] `rb_palette_set_colors()` → `SDL_SetPaletteColors()` (convert 0x00BBGGRR → `SDL_Color`)
- [ ] `rb_palette_get_colors()` → `SDL_GetPaletteColors()` (convert `SDL_Color` → 0x00BBGGRR)

### 2.4 Audio
- [ ] `rb_audio_open()` → `SDL_OpenAudioDevice()` with 22050 Hz, 16-bit stereo
- [ ] `rb_audio_close()` → `SDL_CloseAudioDevice()`
- [ ] `rb_audio_buffer_create()` → allocate PCM buffer + metadata (format, play state, volume, pan, frequency)
- [ ] `rb_audio_buffer_destroy()` → free buffer
- [ ] `rb_audio_buffer_lock()` → return pointer to PCM buffer + available length
- [ ] `rb_audio_buffer_unlock()` → mark buffer ready for mixing
- [ ] `rb_audio_buffer_play()` → set PLAYING flag + start mixing (loop if `D` flag set)
- [ ] `rb_audio_buffer_stop()` → clear PLAYING flag
- [ ] `rb_audio_buffer_set_volume()` → store gain (convert -10000..0 → 0.0..1.0)
- [ ] `rb_audio_buffer_set_pan()` → store L/R scale (convert -10000..10000 → left/right 0.0..1.0)
- [ ] `rb_audio_buffer_set_frequency()` → store target sample rate (flag resampling if differs from master)

### 2.5 Event System
- [ ] `rb_event_wait()` → `SDL_WaitEvent()` + translate to `rb_msg_t`
- [ ] `rb_event_peek()` → `SDL_PeepEvents(..., SDL_GETEVENT)` + translate
- [ ] `rb_event_push()` → translate `rb_msg_t` → `SDL_Event` + `SDL_PushEvent()`

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
| `SDL_QUIT` | `WM_QUIT` (0x001B) |

### 2.6 Input
- [ ] `rb_timer_get_ticks()` → `SDL_GetTicks()`
- [ ] `rb_timer_delay()` → `SDL_Delay()`
- [ ] `rb_joy_count()` → `SDL_NumJoysticks()`
- [ ] `rb_joy_get_caps()` → `SDL_JoystickOpen()` → query axes/buttons/name → `SDL_JoystickClose()`
  - Map axis range: SDL -32768..32767 → WINMM 0..65535
- [ ] `rb_joy_get_state()` → `SDL_JoystickGetAxis()` + `SDL_JoystickGetButton()`
- [ ] `rb_keyboard_get_async_state()` → `SDL_GetKeyboardState()` + VK→Scancode map
  - Return `SHORT`: top bit = pressed, high bit = toggle state
- [ ] `rb_cursor_create()` → `SDL_CreateSystemCursor()` (map IDC_ARROW→0, IDC_CROSS→1, etc.)
- [ ] `rb_cursor_destroy()` → `SDL_FreeCursor()`
- [ ] `rb_cursor_show()` → `SDL_ShowCursor()`

### 2.7 Build
- [ ] Edit `Makefile`: add `-lSDL2 -lSDL2main` to LDFLAGS
- [ ] Edit `Makefile`: add `src/backend/` to source file discovery

### 2.8 Test
- [ ] Compile a minimal C program: `rb_init()` → `rb_window_create("Test", -1, -1, 320, 200, RB_WINDOW_SHOWN)` → `rb_window_show(win, 1)` → `SDL_Delay(2000)` → `rb_window_destroy(win)` → `rb_shutdown()`
- [ ] Expected: SDL2 window appears with title "Test" at 320x200, stays for 2 seconds, then closes

---

## Files
| File | Action |
|------|--------|
| `src/backend/sdl2/*.c` | **New** (~2,000 lines) |
| `Makefile` | Edit: add SDL2 linker flags + source path |

**~2,000 lines, ~8-10 days**
