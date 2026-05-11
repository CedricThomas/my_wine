# Subplan 4: DirectDraw

**Goal**: Mock `IDirectDraw` + `IDirectDrawSurface` vtables (18 + 24 methods). Render via Lock→Write→Unlock→Flip.

**Outcome**: A test PE with `DirectDrawCreate → SetDisplayMode(320,200,8) → CreateSurface → Lock → write checkerboard → Unlock → Flip` shows a checkerboard on screen.

---

## Tasks

### 4.1 ddraw_types.h
- [ ] Create `include/ddraw_types.h` with all DirectDraw 1.x types:
  - `HRESULT` typedef + error codes
  - `DDCOLORKEY`, `DDOVERLAYFX`, `DDPIXELFORMAT`, `DDSCAPS` flags, `DDCAPS`, `DDSURFACEDESC` (v1 and v2)
  - `DDBLTFX` struct
  - `GUID` struct
  - Vtable pointer structures: `IDirectDrawVtbl`, `IDirectDraw`, `IDirectDrawSurfaceVtbl`, `IDirectDrawSurface`, `IDirectDrawPaletteVtbl`, `IDirectDrawPalette`, `IDirectDrawClipperVtbl`, `IDirectDrawClipper`

### 4.2 DirectDrawCreate + IDirectDraw vtable (18 methods)
- [ ] `DirectDrawCreate()` → allocate `IDirectDraw` struct with vtable, return handle
- [ ] `QueryInterface` → stub: `E_NOTIMPL`
- [ ] `AddRef` / `Release` → stub: refcounting
- [ ] `Compact` → stub: `DD_OK`
- [ ] `GetCapabilities` → fill `DDCAPS` (primary, flip, blit, complex)
- [ ] `GetAvailableVidMem` → stub: return fake value
- [ ] `GetMonitorFrequency` → stub: return 60
- [ ] `GetFourCCCodes` → stub: return NULL
- [ ] `SetCooperativeLevel` → **critical**: parse `DDSCL_EXCLUSIVE|DDSCL_FULLSCREEN`, set fullscreen flag
- [ ] `SetDisplayMode` → **critical**: `rb_window_set_fullscreen()` with new dims/bpp
- [ ] `CreateSurface` → **critical**: `rb_surface_create()` or `rb_surface_create_flip_chain()`
  - Handle `DDSCAPS_PRIMARYSURFACE|DDSCAPS_FLIP|DDSCAPS_COMPLEX` → flip chain
  - Handle `DDSCAPS_OFFSCREENPLAIN` → offscreen surface
  - Return mock `IDirectDrawSurface` with vtable
- [ ] `CreatePalette` → `rb_palette_create()`, return mock `IDirectDrawPalette` with vtable
- [ ] `CreateClipper` → stub: return mock `IDirectDrawClipper`
- [ ] `GetDisplayMode` → return current dims/bpp
- [ ] `RestoreDisplayMode` → stub: `DD_OK`
- [ ] `GetDC` / `ReleaseDC` → `rb_window_get_dc()` / release
- [ ] `GetFlipStatus` → stub: `DD_OK`
- [ ] `WaitForVerticalBlank` → stub: `DD_OK`
- [ ] `GetGDIEvent` → stub: return NULL
- [ ] `GetSurfaceFromDC` → stub: `DDERR_UNSUPPORTED`
- [ ] `EnumDisplayModes` → stub: `DD_OK`
- [ ] `CreateSurfaceFromBitmap` → stub: `DDERR_UNSUPPORTED`
- [ ] `DuplicateSurface` → stub: `DDERR_UNSUPPORTED`

### 4.3 IDirectDrawSurface vtable (24 methods)
Each surface gets its own `IDirectDrawSurface` struct with this vtable:
- [ ] `QueryInterface` → stub: `E_NOTIMPL`
- [ ] `AddRef` / `Release` → stub: refcounting
- [ ] `AddAttachedSurface` → stub: `DD_OK`
- [ ] `DeleteAttachedSurface` → stub: `DD_OK`
- [ ] `GetAttachedSurface` → stub: `DD_OK`
- [ ] `AddOverlayDirtyRect` → stub: `DD_OK`
- [ ] `Blt` → **critical**: `rb_surface_blt()` with rect mapping
  - Handle `DDBLT_WAIT` flag (poll until ready)
  - Handle `DDBLT_COLORFILL` → fill rect
  - Handle `DDBLT_DDFPSURFACE` → simple src copy
- [ ] `BltBatch` → stub: `DD_OK`
- [ ] `BltFast` → `rb_surface_blt()` (simplified, no effects)
- [ ] `GetBltStatus` → stub: `DD_OK`
- [ ] `GetDC` → `rb_window_get_dc()` → return mock HDC
- [ ] `ReleaseDC` → `rb_window_release_dc()` → return 1
- [ ] `GetFlipStatus` → stub: `DD_OK`
- [ ] `GetOverlayPosition` → stub: `DD_OK`
- [ ] `SetOverlayPosition` → stub: `DD_OK`
- [ ] `GetSurfaceDesc` → **critical**: fill `DDSURFACEDESC` with `lPitch` + `lpSurface` (raw pixel ptr)
- [ ] `IsLost` → stub: `DD_OK`
- [ ] `Lock` → **critical**: `rb_surface_lock()` → return raw pixel ptr + pitch
  - Store locked rect for `Unlock` to mark dirty
- [ ] `Restore` → stub: `DD_OK` (no mode switch under SDL)
- [ ] `SetClipper` → stub: `DD_OK`
- [ ] `GetClipper` → stub: `DD_OK`
- [ ] `SetPalette` → **critical**: `rb_surface_set_palette()`
- [ ] `Unlock` → **critical**: `rb_surface_unlock()` → mark dirty for `SDL_UpdateWindowSurface`
- [ ] `OverrideCursor` → stub: `DD_OK`
- [ ] `GetOverrideCursor` → stub: `DD_OK`

### 4.4 IDirectDrawPalette vtable (3 methods)
- [ ] `SetEntries` → `rb_palette_set_colors()`
- [ ] `GetEntries` → `rb_palette_get_colors()`
- [ ] `GetColorCount` → return palette color count

### 4.5 IDirectDrawClipper vtable (stub)
- [ ] All methods → stub: `DD_OK` or `DDERR_UNSUPPORTED`

### 4.6 Import Table
- [ ] Add `ddraw.dll` → `DirectDrawCreate` to `import_table.c`

### 4.7 Test
- [ ] Compile a test PE that:
  1. Calls `DirectDrawCreate(NULL, &dd, NULL)`
  2. Calls `dd->lpVtbl->SetCooperativeLevel(dd, hwnd, DDSCL_FULLSCREEN | DDSCL_EXCLUSIVE)`
  3. Calls `dd->lpVtbl->SetDisplayMode(dd, 320, 200, 8)`
  4. Creates a flip chain surface (`DDSCAPS_PRIMARYSURFACE | DDSCAPS_FLIP | DDSCAPS_COMPLEX`)
  5. Locks the back buffer → writes a checkerboard pattern → unlocks
  6. Calls `Flip(NULL, 0)`
- [ ] Expected: SDL2 window appears at 320x200, checkerboard is visible

---

## Files
| File | Action |
|------|--------|
| `include/ddraw_types.h` | **New** (~200 lines) |
| `src/stubs/ddraw_interface.c` | **New** (~350 lines) |
| `src/stubs/ddraw_surface.c` | **New** (~500 lines) |
| `src/stubs/ddraw_palette.c` | **New** (~80 lines) |
| `src/stubs/ddraw_clipper.c` | **New** (~30 lines) |
| `src/loader/import_table.c` | Edit: add `ddraw.dll` entry |

**~1,160 lines, ~4-5 days**
