#include "ddraw_types.h"
#include "user32_types.h"
#include <stdio.h>
#include <string.h>

static int failed = 0;

#define T(cond, msg)                                                           \
    do {                                                                       \
        if (!(cond)) {                                                         \
            fprintf(stderr, "FAIL: %s\n", msg);                                \
            failed++;                                                          \
        }                                                                      \
    } while (0)

int main(void)
{
    LPDIRECTDRAW dd = NULL;
    LPDIRECTDRAW dd_qi = NULL;
    LPDIRECTDRAWPALETTE pal = NULL;
    LPDIRECTDRAWPALETTE pal_from_surface = NULL;
    LPDIRECTDRAWSURFACE primary = NULL;
    LPDIRECTDRAWSURFACE backbuffer = NULL;
    LPDIRECTDRAWSURFACE offscreen = NULL;
    DDSURFACEDESC desc;
    DDSURFACEDESC mode_desc;
    /* DDCAPS caps1;  // unused - GetCaps removed from guest vtable */
    /* DDCAPS caps2;  // unused */
    DDPALETTEENTRY palette_entries[256];
    DDPALETTEENTRY palette_readback[256];
    uint32_t backbuffer_caps = DDSCAPS_BACKBUFFER;
    uint8_t *pixels = NULL;
    DDSURFACEDESC lock_desc;
    uint32_t ref_count;

    T(DirectDrawCreate(NULL, &dd, NULL) == DD_OK, "DirectDrawCreate failed");
    T(dd != NULL, "DirectDrawCreate returned NULL interface");

    if (dd) {
        T(dd->lpVtbl->QueryInterface(dd, &IID_IDirectDraw, (void **)&dd_qi) == DD_OK,
          "DirectDraw QueryInterface failed");
        T(dd_qi == dd, "DirectDraw QueryInterface returned unexpected pointer");
        ref_count = dd->lpVtbl->AddRef(dd);
        T(ref_count >= 2, "DirectDraw AddRef returned unexpected refcount");
        T(dd->lpVtbl->Release(dd) == ref_count - 1, "DirectDraw Release after AddRef failed");

        T(dd->lpVtbl->SetCooperativeLevel(dd, NULL, DDSCL_NORMAL) == DD_OK,
          "SetCooperativeLevel failed");
        T(dd->lpVtbl->SetDisplayMode(dd, 64, 48, 8) == DD_OK,
          "SetDisplayMode failed");
        memset(&mode_desc, 0, sizeof(mode_desc));
        mode_desc.ddSize = sizeof(mode_desc);
        T(dd->lpVtbl->GetDisplayMode(dd, &mode_desc) == DD_OK, "GetDisplayMode failed");
        T(mode_desc.lWidth == 64 && mode_desc.lHeight == 48,
          "GetDisplayMode dimensions mismatch");
        /* GetCaps removed from guest-compatible vtable layout */
        /*
        memset(&caps1, 0, sizeof(caps1));
        memset(&caps2, 0, sizeof(caps2));
        T(dd->lpVtbl->GetCaps(dd, &caps1, &caps2) == DD_OK, "GetCaps failed");
        T(caps1.dwPaletteEntries == 256, "GetCaps palette entries mismatch");
        T((caps1.dwCaps & DDCAPS_FLIP) != 0, "GetCaps missing flip capability");
        */

        for (int i = 0; i < 256; i++) {
            palette_entries[i].peRed = (uint8_t)i;
            palette_entries[i].peGreen = (uint8_t)i;
            palette_entries[i].peBlue = (uint8_t)i;
            palette_entries[i].peFlags = 0;
        }

        T(dd->lpVtbl->CreatePalette(dd, DDPCAPS_8BIT | DDPCAPS_INITIALIZE,
                                    palette_entries, (void **)&pal, NULL) == DD_OK,
          "CreatePalette failed");
        T(pal != NULL, "CreatePalette returned NULL");
        if (pal) {
            memset(palette_readback, 0, sizeof(palette_readback));
            T(pal->lpVtbl->GetEntries(pal, NULL, 0, 256, palette_readback) == DD_OK,
              "palette GetEntries failed");
            T(palette_readback[0].peRed == 0 && palette_readback[255].peBlue == 255,
              "palette GetEntries data mismatch");
            palette_entries[3].peRed = 0x12;
            palette_entries[3].peGreen = 0x34;
            palette_entries[3].peBlue = 0x56;
            T(pal->lpVtbl->SetEntries(pal, NULL, 3, 1, &palette_entries[3]) == DD_OK,
              "palette SetEntries failed");
            memset(palette_readback, 0, sizeof(palette_readback));
            T(pal->lpVtbl->GetEntries(pal, NULL, 3, 1, &palette_readback[3]) == DD_OK,
              "palette GetEntries after SetEntries failed");
            T(palette_readback[3].peRed == 0x12 &&
              palette_readback[3].peGreen == 0x34 &&
              palette_readback[3].peBlue == 0x56,
              "palette SetEntries readback mismatch");
        }

        memset(&desc, 0, sizeof(desc));
        desc.ddSize = sizeof(desc);
        desc.ddFlags = DDSD_CAPS | DDSD_WIDTH | DDSD_HEIGHT | DDSD_BACKBUFFERCOUNT;
        desc.ddCaps = DDSCAPS_PRIMARYSURFACE | DDSCAPS_FLIP | DDSCAPS_COMPLEX;
        desc.lWidth = 64;
        desc.lHeight = 48;
        desc.dwBackBufferCount = 1;

        T(dd->lpVtbl->CreateSurface(dd, &desc, (void **)&primary, NULL) == DD_OK,
          "CreateSurface(primary) failed");
        T(primary != NULL, "primary surface is NULL");

        if (primary) {
            T(primary->lpVtbl->GetAttachedSurface(primary, &backbuffer_caps,
                                                 (void **)&backbuffer) == DD_OK,
              "GetAttachedSurface(backbuffer) failed");
            T(backbuffer != NULL, "backbuffer surface is NULL");

            T(primary->lpVtbl->SetPalette(primary, pal) == DD_OK,
              "primary SetPalette failed");
            T(primary->lpVtbl->GetPalette(primary, (void **)&pal_from_surface) == DD_OK,
              "primary GetPalette failed");
            T(pal_from_surface == pal, "primary GetPalette returned unexpected palette");

            memset(&desc, 0, sizeof(desc));
            desc.ddSize = sizeof(desc);
            desc.ddFlags = DDSD_CAPS | DDSD_WIDTH | DDSD_HEIGHT;
            desc.ddCaps = DDSCAPS_OFFSCREENPLAIN;
            desc.lWidth = 64;
            desc.lHeight = 48;

            T(dd->lpVtbl->CreateSurface(dd, &desc, (void **)&offscreen, NULL) == DD_OK,
              "CreateSurface(offscreen) failed");
            T(offscreen != NULL, "offscreen surface is NULL");
        }

        if (offscreen) {
            memset(&lock_desc, 0, sizeof(lock_desc));
            lock_desc.ddSize = sizeof(lock_desc);
            T(offscreen->lpVtbl->Lock(offscreen, NULL, &lock_desc, 0, NULL) == DD_OK,
              "offscreen Lock failed");
            pixels = (uint8_t *)(uintptr_t)lock_desc.lpSurface;
            T(pixels != NULL, "offscreen Lock returned NULL pixels");
            T(lock_desc.lPitch >= 64, "offscreen pitch too small");

            if (pixels) {
                for (uint32_t y = 0; y < 48; y++)
                    memset(pixels + y * lock_desc.lPitch, 0x2A, 64);
            }

            T(offscreen->lpVtbl->Unlock(offscreen, NULL) == DD_OK,
              "offscreen Unlock failed");
        }

        if (backbuffer && offscreen) {
            T(backbuffer->lpVtbl->Blt(backbuffer, NULL, offscreen, NULL, 0, NULL) == DD_OK,
              "Blt to backbuffer failed");
            T(backbuffer->lpVtbl->BltFast(backbuffer, 0, 0, offscreen, NULL, 0) == DD_OK,
              "BltFast to backbuffer failed");
            pixels = NULL;
            memset(&lock_desc, 0, sizeof(lock_desc));
            lock_desc.ddSize = sizeof(lock_desc);
            T(backbuffer->lpVtbl->Lock(backbuffer, NULL, &lock_desc, 0, NULL) == DD_OK,
              "backbuffer Lock failed");
            pixels = (uint8_t *)(uintptr_t)lock_desc.lpSurface;
            T(pixels != NULL, "backbuffer Lock returned NULL pixels");
            T(backbuffer->lpVtbl->Unlock(backbuffer, NULL) == DD_OK, "backbuffer Unlock failed");
            T(primary->lpVtbl->Flip(primary, NULL, 0) == DD_OK,
              "Flip failed");
        }

        if (primary) {
            memset(&desc, 0, sizeof(desc));
            desc.ddSize = sizeof(desc);
            T(primary->lpVtbl->GetSurfaceDesc(primary, &desc) == DD_OK,
              "GetSurfaceDesc failed");
            T(desc.lWidth == 64 && desc.lHeight == 48, "surface desc dimensions mismatch");
            T(desc.lPitch >= 64, "surface desc pitch too small");

            pixels = NULL;
            memset(&lock_desc, 0, sizeof(lock_desc));
            lock_desc.ddSize = sizeof(lock_desc);
            T(primary->lpVtbl->Lock(primary, NULL, &lock_desc, 0, NULL) == DD_OK,
              "primary Lock failed");
            pixels = (uint8_t *)(uintptr_t)lock_desc.lpSurface;
            T(pixels != NULL, "primary Lock returned NULL pixels");
            T(primary->lpVtbl->Unlock(primary, NULL) == DD_OK, "primary Unlock failed");
        }
    }

    if (dd)
        dd->lpVtbl->Release(dd);
    if (dd_qi)
        dd_qi->lpVtbl->Release(dd_qi);
    if (offscreen)
        offscreen->lpVtbl->Release(offscreen);
    if (backbuffer)
        backbuffer->lpVtbl->Release(backbuffer);
    if (primary)
        primary->lpVtbl->Release(primary);
    if (pal_from_surface)
        pal_from_surface->lpVtbl->Release(pal_from_surface);
    if (pal)
        pal->lpVtbl->Release(pal);

    if (failed == 0)
        printf("PASS: DirectDraw test passed\n");
    else
        printf("FAIL: %d DirectDraw test(s) failed\n", failed);
    return failed;
}
