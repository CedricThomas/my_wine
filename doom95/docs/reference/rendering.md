# Rendering

## Surface Architecture: Hardware Flip Chain with Offscreen Surfaces

DDCAPS/DDSCAPS constants in the DGROUP section reveal a sophisticated surface layout:

| Cap | Count | Meaning |
|-----|-------|---------|
| DDCAPS_FLIP | 33 | Hardware surface flipping |
| DDCAPS_BLT | 275 | Hardware blitting |
| DDCAPS_BLTHW | 216 | Hardware blitter available |
| DDCAPS_VIDEOMEMORY | 213 | VRAM surfaces |
| DDSCAPS_FRONTBUFFER | 167 | Primary surface |
| DDSCAPS_BACKBUFFER | 311 | Multiple back buffers |
| DDSCAPS_COMPLEX | 312 | Flip chain (primary + backs) |
| DDSCAPS_FLIP | 168 | Surface flipping |

## Surface Hierarchy (from global variable names)

- **`lpDD`** — `IDirectDraw*` interface pointer
- **`lpDDSPrimary`** — Primary surface with front buffer
- **`lpDDSBack`** — Back buffer(s) for flip chain
- **`lpDDSOff`** — Offscreen surface for rendering
- **`lpDDSOffFlat`** — Flat offscreen surface (system memory fallback)
- **`lpDDSPage4`** — 4th page/back buffer
- **`lpDDSFlash`** — Flash/burst effect surface
- **`lpDDPal`** — `IDirectDrawPalette*`
- **`lpClipper`** — `IDirectDrawClipper*`

## Rendering Pattern

The game uses a **3D-style software renderer** with this pattern:

1. Render to offscreen surface (`lpDDSOff` or `lpDDSOffFlat`)
2. **`Lock`/`Unlock`** to get direct memory access ("megalock")
3. Render the frame to the locked surface (the "MegaLock" terminology is DOOM95-specific)
4. **Flip** or **Blt** from offscreen to back buffer
5. **Flip** the back buffer to primary

## Resolution

- **320×200** (classic DOOM resolution) at **8-bit (256-color)** with palette management (default)
- **640×400** @ 8-bit (high res mode, likely)
- Possibly **320×200** @ 16-bit (via dithering, though less likely for DOOM95)

## Fallback Chain

VRAM flip chain → system memory flip chain → software blt

Error strings confirming the pattern:
```
"R_RenderPlayerView1: megalock failed"
"V_DrawPatchDirect: megalock failed"
"Couldn't create primary flipping surface with two back buffers in vram"
"Couldn't create primary flipping surface with one back buffer in vram"
"Couldn't create primary flipping surface with one back buffers in any ram"
"Rendering to system memory offscreen surface"
"EraseSurface: blt returned %d"
```

## Palette / Color Mode

The game uses **8-bit (256-color) palettized rendering** for the game world, with:
- DDraw palette management for the fullscreen display
- GDI palette for dialog rendering
- Palette transitions (fade in/out between areas)

GDI imports confirm: `CreatePalette`, `RealizePalette`, `SelectPalette`, `GetSystemPaletteEntries`
