# Subplan 1: Foundation

**Goal**: PE32 loader support + handle manager + `render_backend.h` header + Watcom CRT bypass.

**Outcome**: `./my_wine DOOM95.EXE` parses headers, maps sections, resolves imports, and jumps to `D_DoomMain` without crashing.

---

## Tasks

### 1.1 PE32 Detection
- [ ] Add `IMAGE_NT_HEADERS32` struct to `include/pe.h`
- [ ] In `src/pe_headers.c`: detect PE32 vs PE32+ via magic field (`0x10b` = PE32, `0x20b` = PE32+)
- [ ] Return a flag or union so downstream code knows which thunk size to use

### 1.2 PE32 Import Parsing
- [ ] In `src/pe_imports.c`: read `IMAGE_THUNK_DATA32` (4-byte) instead of `IMAGE_THUNK_DATA64` (8-byte)
- [ ] Handle 32-bit import directory (`OriginalFirstThunk`, `FirstThunk`, `Name` as 32-bit RVAs)
- [ ] Handle ordinal imports (high bit set in 32-bit thunk → lower 16 bits = ordinal)

### 1.3 PE32 Import Resolution
- [ ] In `src/loader/import_resolve.c`: write 4-byte IAT entries (not 8-byte) for PE32 images
- [ ] Adjust thunk scanning for 32-bit jump displacements

### 1.4 PE32 TEB/PEB and Image Mapping
- [ ] In `src/loader/teb_peb.c`: use 32-bit PEB/TEB offsets for PE32 images
- [ ] In `src/loader/image_mapper.c`: map 32-bit image at `0x00400000`, handle 32-bit section alignment
- [ ] Ensure GS base works for 32-bit image space

### 1.5 Watcom CRT Bypass
- [ ] In `src/main.c`: resolve `D_DoomMain` address from the Watcom CRT jump table at `0x00449d6c`
- [ ] Use `D_DoomMain` as the entry point instead of the PE entry point (`0x000444d8`)
- [ ] Seed `GetCommandLineA` with host argv (game uses this for args instead of CRT)

### 1.6 Handle Manager
- [ ] Create `include/handle_manager.h`: `wine_handle_alloc(type, size)`, `wine_handle_free(id)`, `wine_handle_get(id)`
- [ ] Create `src/common/handle_manager.c`: type-tagged handle table, ref counting, safe iteration
- [ ] Define handle type tags:
  - 0x01: HWND, 0x02: HDC, 0x03: HBITMAP, 0x04: HPALETTE, 0x05: HFONT, 0x06: HCURSOR
  - 0x10: IDirectDraw, 0x11: IDirectDrawSurface, 0x12: IDirectDrawPalette, 0x13: IDirectDrawClipper
  - 0x20: IDirectSound, 0x21: IDirectSoundBuffer
  - 0x30: HMIDISTREAM, 0x31: HMIDIOUT
  - 0x40: HMODULE, 0x41: HRSRC, 0x42: HGLOBAL/HLOCAL
  - 0x50: HKEY, 0x60: HHOOK
- [ ] Migrate existing handle types (files, events, mutexes, threads) to the new handle manager

### 1.7 render_backend.h
- [ ] Create `include/render_backend.h` with all 72 function declarations:
  - Init/Shutdown: `rb_init()`, `rb_shutdown()`
  - Window: `rb_window_create`, `rb_window_destroy`, `rb_window_show`, `rb_window_set_position`, `rb_window_set_size`, `rb_window_set_title`, `rb_window_get_rect`, `rb_window_get_client_rect`, `rb_window_set_fullscreen`, `rb_window_get_dc`, `rb_window_release_dc`, `rb_window_set_cursor`, `rb_window_warp_mouse`
  - Surface: `rb_surface_create`, `rb_surface_create_flip_chain`, `rb_surface_destroy`, `rb_surface_lock`, `rb_surface_unlock`, `rb_surface_blt`, `rb_surface_flip`, `rb_surface_get_desc`, `rb_surface_set_palette`
  - Palette: `rb_palette_create`, `rb_palette_destroy`, `rb_palette_set_colors`, `rb_palette_get_colors`
  - Cursor: `rb_cursor_create`, `rb_cursor_destroy`, `rb_cursor_show`
  - Event: `rb_event_wait`, `rb_event_peek`, `rb_event_push`
  - Audio: `rb_audio_open`, `rb_audio_close`, `rb_audio_buffer_create`, `rb_audio_buffer_destroy`, `rb_audio_buffer_lock`, `rb_audio_buffer_unlock`, `rb_audio_buffer_play`, `rb_audio_buffer_stop`, `rb_audio_buffer_set_volume`, `rb_audio_buffer_set_pan`, `rb_audio_buffer_set_frequency`
  - Timer: `rb_timer_get_ticks`, `rb_timer_delay`
  - Joystick: `rb_joy_count`, `rb_joy_get_caps`, `rb_joy_get_state`
  - Keyboard: `rb_keyboard_get_async_state`

### 1.8 Test
- [ ] Run `./my_wine samples/unpacked/doom95/DOOM95.EXE`
- [ ] Expected: parses PE32 headers, maps sections, resolves imports, reaches entry point
- [ ] Expected: will crash at first API call (no stubs yet), but loader itself succeeds

---

## Files
| File | Action |
|------|--------|
| `include/pe.h` | Edit: add `IMAGE_NT_HEADERS32` |
| `src/pe_headers.c` | Edit: add PE32 detection |
| `src/pe_imports.c` | Edit: add `IMAGE_THUNK_DATA32` parsing |
| `src/loader/import_resolve.c` | Edit: add 32-bit IAT writes |
| `src/loader/teb_peb.c` | Edit: add 32-bit PEB/TEB offsets |
| `src/loader/image_mapper.c` | Edit: add 32-bit section mapping |
| `src/main.c` | Edit: bypass Watcom CRT → `D_DoomMain` |
| `include/handle_manager.h` | **New** |
| `src/common/handle_manager.c` | **New** |
| `include/render_backend.h` | **New** |

**~870 lines, ~3-4 days**
