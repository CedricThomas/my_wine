# gdi32.dll — 16 functions

## Device Context (3)

| Function | Category | SDL2 Mapping | Notes |
|----------|----------|-------------|-------|
| `CreateDCA` | critical | `SDL_GetWindowSurface()` (via mock HDC) | "Display DC" → wrap the window surface |
| `DeleteDC` | critical | No-op on mock HDC | Return TRUE |
| `GetDC` | (from user32, shared) | `SDL_GetWindowSurface()` wrapper | Returns mock HDC wrapping SDL_Surface* |

## Bitmap / Surface (4)

| Function | Category | SDL2 Mapping | Notes |
|----------|----------|-------------|-------|
| `CreateDIBitmap` | critical | `SDL_CreateRGBSurface()` + `SDL_ConvertPixels()` | Map BITMAPINFOHEADER→SDL_PixelFormat+surface. Handle BI_RGB only. |
| `StretchDIBits` | critical | `SDL_BlitSurface()` or `SDL_SoftStretch()` | **Critical.** Software rendering fallback. Map source DIB→SDL_Surface→blit with scaling. SRCCOPY = simple blit. |
| `GetObjectA` | critical | Return stored metrics | For HBITMAP→BITMAP struct (width,height,bits ptr). For HFONT→LOGFONT (height). |
| `DeleteObject` | critical | `SDL_FreeSurface()` / `SDL_FreePalette()` / free | Type-tagged: bitmap→FreeSurface, palette→FreePalette, font→free |

## Palette (5)

| Function | Category | SDL2 Mapping | Notes |
|----------|----------|-------------|-------|
| `CreatePalette` | critical | `SDL_Palette*` + `SDL_SetPaletteColors()` | Map LOGPALETTE→SDL_Color[], create SDL_Palette*. Associate with HDC. |
| `SelectPalette` | critical | Swap SDL_Palette* on HDC | Store active palette per HDC |
| `RealizePalette` | critical | `SDL_SetPaletteColors()` on surface | Apply palette to surfaces created under this HDC |
| `GetSystemPaletteEntries` | critical | `SDL_GetPaletteColors()` | Copy current palette colors to output buffer |
| `UnrealizeObject` | cosmetic | Stub (return TRUE) | No-op |

## Font (1)

| Function | Category | SDL2 Mapping | Notes |
|----------|----------|-------------|-------|
| `CreateFontA` | critical | Stub (return sentinel HFONT) | Store font metrics (height) for GetObjectA. Game uses GDI text rarely. |

## Display Info (2)

| Function | Category | SDL2 Mapping | Notes |
|----------|----------|-------------|-------|
| `GetDeviceCaps` | critical | Hardcoded or `SDL_GetCurrentDisplayMode()` | BITSPIXEL→16/32, DESKTOPHEIGHT/WIDTH→display size. Most→0 or 1. |
| `GetStockObject` | critical | Stub (return sentinel handle) | WHITEBRUSH=1, BLACKPEN=2, etc. Return valid-but-dummy handle |

## Text Color (2)

| Function | Category | SDL2 Mapping | Notes |
|----------|----------|-------------|-------|
| `SetBkColor` | cosmetic | Store color on HDC | Trivial: store COLORREF for text rendering |
| `SetTextColor` | cosmetic | Store color on HDC | Same |

---

## Key Observations

- **StretchDIBits** is the most critical GDI function. DOOM95 may use it as the software rendering fallback when DirectDraw is unavailable.
- Palette functions must coordinate with SDL2's `SDL_Palette` for 8-bit surfaces.
- All GDI handles (HDC, HBITMAP, HFONT, HPALETTE) must be allocated with integer IDs and tracked in an internal table.
