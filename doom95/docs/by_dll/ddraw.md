# ddraw.dll — 1 factory + ~42 vtable methods

All rendering flows through `DirectDrawCreate` → `IDirectDraw` vtable → `IDirectDrawSurface` vtable.

## Factory (1)

| Function | Category | SDL2 Mapping | Notes |
|----------|----------|-------------|-------|
| `DirectDrawCreate` | critical | **Custom mock vtable** | Return `IDirectDraw` interface (struct of function pointers). Game calls all subsequent DDraw methods through this vtable. |

---

## IDirectDraw Vtable (18 methods)

| Method | Category | SDL2 Mapping | Notes |
|--------|----------|-------------|-------|
| `SetCooperativeLevel` | critical | Stub | Parse DDSCL_EXCLUSIVE\|DDSCL_FULLSCREEN flag |
| `SetDisplayMode` | critical | `SDL_SetWindowFullscreen()` + `SDL_CreateWindow()` | Map w/h/bpp→new window or fullscreen toggle |
| `CreateSurface` | critical | `SDL_CreateRGBSurface()` | Map DDSURFACEDESC→SDL surface. Track surface handles. |
| `CreatePalette` | critical | `SDL_Palette*` creation | Map DDPALETTE→SDL_Palette |
| `CreateClipper` | critical | Stub | Return mock IDirectDrawClipper |
| `GetAvailableVidMem` | stub | Stub | Return fake value |
| `GetMonitorFrequency` | stub | Stub | Return 60 |
| `GetFourCCCodes` | stub | Stub | Return NULL |
| `GetCaps` | stub | Fill DDCAPS struct | Set DDCAPS_PRIMARYSURFACE, DDCAPS_FLIP, DDCAPS_BLT, etc. |
| `RestoreDisplayMode` | stub | Stub | DD_OK |
| `GetDisplayMode` | stub | Return current dims/bpp | From current window state |
| `GetDC` | stub | Return mock HDC | Shared with GDI path |
| `ReleaseDC` | stub | No-op | Return DD_OK |
| `Compact` | stub | Stub | DD_OK |
| `GetFlipStatus` | stub | Stub | DD_OK |
| `WaitForVerticalBlank` | stub | Stub | DD_OK |
| `GetGDIEvent` | stub | Stub | Return NULL |
| `GetSurfaceFromDC` | stub | Stub | DDERR_UNSUPPORTED |
| `EnumDisplayModes` | stub | Stub | DD_OK |

---

## IDirectDrawSurface Vtable (24 methods)

| Method | Category | SDL2 Mapping | Notes |
|--------|----------|-------------|-------|
| `Lock` | critical | Access surface pixels directly | Return pointer to SDL_Surface->pixels. Store pitch. **DOOM95 writes directly here.** |
| `Unlock` | critical | No-op (mark surface dirty) | SDL surfaces don't require unlock. Mark for SDL_UpdateWindowSurface. |
| `Blt` | critical | `SDL_BlitSurface()` or `SDL_BlitScaled()` | Rect-to-rect copy. Map DDBLTFX (color fill)→SDL_FillRect() or blit. |
| `BltFast` | critical | `SDL_BlitSurface()` | Simplified blit without effects |
| `GetSurfaceDesc` | critical | Copy surface metadata | Return DDSURFACEDESC with lPitch, lpSurface (pixel data ptr) |
| `SetPalette` | critical | `SDL_SetPaletteColors()` | Map LPPALETTEENTRY[]→SDL_Color[]. Propagate to all surfaces. |
| `Restore` | stub | No-op | No video mode switch in SDL2. Return DD_OK. |
| `GetDC` | stub | Return mock HDC | Shared with GDI path |
| `ReleaseDC` | stub | No-op | Return DD_OK |
| `IsLost` | stub | Stub | Return DD_OK |
| `AddAttachedSurface` | stub | Stub | DD_OK |
| `DeleteAttachedSurface` | stub | Stub | DD_OK |
| `GetAttachedSurface` | stub | Stub | DD_OK |
| `AddOverlayDirtyRect` | stub | Stub | Overlay not used |
| `GetBltStatus` | stub | Stub | DD_OK |
| `GetFlipStatus` | stub | Stub | DD_OK |
| `SetOverlayPosition` | stub | Stub | DD_OK |
| `GetOverlayPosition` | stub | Stub | DD_OK |
| `SetAttachedSurface` | stub | Stub | DD_OK |
| `SetClipper` | stub | Stub | DD_OK |
| `GetClipper` | stub | Stub | DD_OK |
| `OverrideCursor` | stub | Stub | DD_OK |
| `GetOverrideCursor` | stub | Stub | DD_OK |
| `BltBatch` | stub | Stub | DD_OK |

---

## Surface Hierarchy (from runtime analysis)

- **`lpDD`** — `IDirectDraw*` interface pointer
- **`lpDDSPrimary`** — Primary surface with front buffer
- **`lpDDSBack`** — Back buffer(s) for flip chain
- **`lpDDSOff`** — Offscreen surface for rendering
- **`lpDDSOffFlat`** — Flat offscreen (system memory fallback)
- **`lpDDSPage4`** — 4th page/back buffer
- **`lpDDSFlash`** — Flash/burst effect surface
- **`lpDDPal`** — `IDirectDrawPalette*`
- **`lpClipper`** — `IDirectDrawClipper*`

## Rendering Pattern

1. Render to offscreen surface (`lpDDSOff` or `lpDDSOffFlat`)
2. **Lock** → direct memory access ("megalock")
3. Write pixels (DOOM95's software renderer)
4. **Unlock**
5. **Blt** or **Flip** from offscreen to back buffer
6. **Flip** back buffer to primary (display)

## Fallback Chain

VRAM flip chain → system memory flip chain → software blt
