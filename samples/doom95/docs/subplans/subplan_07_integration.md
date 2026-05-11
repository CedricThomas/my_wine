# Subplan 7: Final Integration

**Goal**: Dialog system for DOOM95 menus, GDI rendering support, end-to-end DOOM95 test.

**Outcome**: DOOM95 runs from title screen → game loop → menus work → keyboard input → sound effects.

---

## Tasks

### 7.1 Dialog System (user32_dialog.c)
- [ ] `CreateDialogParamA` → parse dialog resource from PE `.rsrc` (IDs 104, 130, 131)
  - Build in-memory control tree
  - Return mock `HWND` for the dialog
- [ ] `IsDialogMessageA` → route messages to dialog `WNDPROC` if active
- [ ] `GetDlgItem` → lookup control from dialog + ID
- [ ] `CheckDlgButton` → store checkbox state per control
- [ ] `IsDlgButtonChecked` → return checkbox state
- [ ] `SetDlgItemTextA` → set text label per control
- [ ] `MessageBoxA` → `SDL_ShowSimpleMessageBox()`

### 7.2 GDI (gdi32_surface.c, gdi32_palette.c, gdi32_font.c)
- [ ] `CreateDCA` → return mock `HDC` wrapping `rb_window_get_dc()`
- [ ] `GetDC` → same as `CreateDCA` (mock `HDC`)
- [ ] `ReleaseDC` → `rb_window_release_dc()` → return 1
- [ ] `DeleteDC` → return TRUE
- [ ] `CreateDIBitmap` → `rb_surface_create()` from `BITMAPINFOHEADER`
- [ ] `StretchDIBits` → `rb_surface_blt()` or `rb_surface_create()` + `rb_surface_blt()` for scaling
  - **Critical for GDI fallback** when DDraw is unavailable
- [ ] `GetObjectA` → return stored metrics from `HBITMAP`/`HFONT`
- [ ] `DeleteObject` → type-tagged: `rb_surface_destroy()` for bitmaps, `rb_palette_destroy()` for palettes, `free()` for fonts
- [ ] `GetDeviceCaps` → map index to hardcoded/display values:
  - `BITSPIXEL`(12) → 8 or 32, `PLANES`(14) → 1, `DESKTOPVERTRES`(117) → display height, `DESKTOPHORZRES`(118) → display width
- [ ] `GetStockObject` → return sentinel handle based on index (WHITEBRUSH=1, BLACKPEN=2, etc.)
- [ ] `CreatePalette` → `rb_palette_create()`
- [ ] `SelectPalette` → swap `SDL_Palette` on HDC
- [ ] `RealizePalette` → `rb_surface_set_palette()` on window surface
- [ ] `GetSystemPaletteEntries` → `rb_palette_get_colors()`
- [ ] `SetBkColor` / `SetTextColor` → store color in HDC struct
- [ ] `UnrealizeObject` → return TRUE
- [ ] `CreateFontA` → store font metrics (height) in `HFONT`; return sentinel

### 7.3 Import Tables
- [ ] Add all `gdi32.dll` entries to `import_table.c` (16 functions)
- [ ] Ensure all user32 dialog entries are in `import_table.c`

### 7.4 DOOM95 Init Test
- [ ] Launch `./my_wine samples/doom95/unpacked/DOOM95.EXE`
- [ ] Expected: no crash during init (window created, DDraw init, DSound init, MIDI init, registry access)
- [ ] Expected: SDL window appears (may be empty or show title screen)
- [ ] Expected: init messages/errors don't crash (registry stubs return `ERROR_SUCCESS`, DPlay returns graceful error)

### 7.5 DOOM95 Game Loop Test
- [ ] Launch DOOM95.EXE from title screen
- [ ] Expected: game world is rendered (even if colors are wrong initially)
- [ ] Expected: player movement works (keyboard input via `GetAsyncKeyState`)
- [ ] Expected: sound effects play (DirectSound)
- [ ] Expected: no repeated crashes
- [ ] Expected: FPS is reasonable (≥ 15 fps acceptable for first pass)

### 7.6 DOOM95 Menus Test
- [ ] From title screen, attempt to open options menu
- [ ] Expected: dialog is created (doesn't crash)
- [ ] Expected: control state can be read/written
- [ ] Expected: menu can be closed
- [ ] Expected: episode selection works
- [ ] (Visual rendering of dialog controls may be incomplete — state tracking is the priority)

---

## Files
| File | Action |
|------|--------|
| `src/stubs/user32_dialog.c` | **New** (~250 lines) |
| `src/stubs/gdi32_surface.c` | **New** (~250 lines) |
| `src/stubs/gdi32_palette.c` | **New** (~120 lines) |
| `src/stubs/gdi32_font.c` | **New** (~40 lines) |
| `src/loader/import_table.c` | Edit: add gdi32.dll entries |

**~660 lines, ~4-5 days**
