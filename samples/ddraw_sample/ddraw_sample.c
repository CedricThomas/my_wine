#include <windows.h>
#include <string.h>

typedef struct _DDGUID {
    DWORD Data1;
    WORD Data2;
    WORD Data3;
    BYTE Data4[8];
} DDGUID;

typedef struct _DDSURFACEDESC {
    DWORD ddSize;
    DWORD ddFlags;
    DWORD ddCaps;
    DWORD ddX;
    DWORD ddY;
    union {
        LONG lPitch;
        DWORD lWidth;
    };
    DWORD dwBackBufferCount;
    union {
        DWORD wWidth;
        DWORD wHeight;
        DWORD lWidth2;
        DWORD lHeight;
    };
    union {
        DWORD lpSurface;
        DWORD lpDDSurfaceDesc;
    };
} DDSURFACEDESC;

typedef struct _IDirectDraw IDirectDraw;
typedef struct _IDirectDrawSurface IDirectDrawSurface;
typedef struct _IDirectDrawPalette IDirectDrawPalette;

/* IDirectDrawVtbl — must match the host vtable layout exactly.
 * Each slot offset and parameter count must match the host IDirectDrawVtbl
 * so that calling through the guest struct hits the right host function. */
typedef struct _IDirectDrawVtbl {
    /*  0 */ HRESULT (WINAPI *QueryInterface)(void *, const void *, void **);
    /*  1 */ ULONG (WINAPI *AddRef)(void *);
    /*  2 */ ULONG (WINAPI *Release)(void *);
    /*  3 */ HRESULT (WINAPI *Compact)(void *);
    /*  4 */ HRESULT (WINAPI *CreateClipper)(void *, DWORD, void **, void *);
    /*  5 */ HRESULT (WINAPI *CreatePalette)(void *, DWORD, void *, void **, void *);
    /*  6 */ HRESULT (WINAPI *CreateSurface)(void *, void *, void **, void *);
    /*  7 */ HRESULT (WINAPI *DuplicateSurface)(void *, void *, void **);
    /*  8 */ HRESULT (WINAPI *EnumDisplayModes)(void *, DWORD, void *, void *, void *);
    /*  9 */ HRESULT (WINAPI *EnumSurfaces)(void *, DWORD, void *, void *, void *);
    /* 10 */ HRESULT (WINAPI *FlipToGDISurface)(void *);
    /* 11 */ HRESULT (WINAPI *GetCaps)(void *, void *, void *);
    /* 12 */ HRESULT (WINAPI *GetDisplayMode)(void *, void *);
    /* 13 */ HRESULT (WINAPI *GetFourCCCodes)(void *, DWORD *, DWORD *);
    /* 14 */ HRESULT (WINAPI *GetGDISurface)(void *, void **);
    /* 15 */ HRESULT (WINAPI *GetMonitorFrequency)(void *, DWORD *);
    /* 16 */ HRESULT (WINAPI *GetScanLine)(void *, DWORD *);
    /* 17 */ HRESULT (WINAPI *GetVerticalBlankStatus)(void *, DWORD *);
    /* 18 */ HRESULT (WINAPI *Initialize)(void *, void *);
    /* 19 */ HRESULT (WINAPI *RestoreDisplayMode)(void *);
    /* 20 */ HRESULT (WINAPI *RestoreAllSurfaces)(void *);
    /* 21 */ HRESULT (WINAPI *SetCooperativeLevel)(void *, void *, DWORD);
    /* 22 */ HRESULT (WINAPI *SetDisplayMode)(void *, DWORD, DWORD, DWORD);
    /* 23 */ HRESULT (WINAPI *WaitForVerticalBlank)(void *, DWORD, void *);
} IDirectDrawVtbl;

/* IDirectDrawSurfaceVtbl — must match the host vtable layout exactly.
 * Each slot offset must match the host IDirectDrawSurfaceVtbl
 * so that calling through the guest struct hits the right host function. */
typedef struct _IDirectDrawSurfaceVtbl {
    /*  0 */ HRESULT (WINAPI *QueryInterface)(void *, const void *, void **);
    /*  1 */ ULONG (WINAPI *AddRef)(void *);
    /*  2 */ ULONG (WINAPI *Release)(void *);
    /*  3 */ HRESULT (WINAPI *AddAttachedSurface)(void *, void *);
    /*  4 */ HRESULT (WINAPI *AddOverlayDirtyRect)(void *, void *);
    /*  5 */ HRESULT (WINAPI *Blt)(void *, void *, void *, void *, DWORD, void *);
    /*  6 */ HRESULT (WINAPI *BltBatch)(void *, void *, DWORD, DWORD);
    /*  7 */ HRESULT (WINAPI *BltFast)(void *, DWORD, DWORD, void *, void *, DWORD);
    /*  8 */ HRESULT (WINAPI *DeleteAttachedSurface)(void *, DWORD, void *);
    /*  9 */ HRESULT (WINAPI *EnumAttachedSurfaces)(void *, void *, void *);
    /* 10 */ HRESULT (WINAPI *EnumOverlayZOrders)(void *, DWORD, void *, void *);
    /* 11 */ HRESULT (WINAPI *Flip)(void *, void *, DWORD);
    /* 12 */ HRESULT (WINAPI *GetAttachedSurface)(void *, void *, void **);
    /* 13 */ HRESULT (WINAPI *GetBltStatus)(void *, DWORD);
    /* 14 */ HRESULT (WINAPI *GetCaps)(void *, void *);
    /* 15 */ HRESULT (WINAPI *GetClipper)(void *, void **);
    /* 16 */ HRESULT (WINAPI *GetColorKey)(void *, DWORD, void *);
    /* 17 */ HRESULT (WINAPI *GetDC)(void *, void **);
    /* 18 */ HRESULT (WINAPI *GetFlipStatus)(void *, DWORD);
    /* 19 */ HRESULT (WINAPI *GetOverlayPosition)(void *, LONG *, LONG *);
    /* 20 */ HRESULT (WINAPI *GetPalette)(void *, void **);
    /* 21 */ HRESULT (WINAPI *GetPixelFormat)(void *, void *);
    /* 22 */ HRESULT (WINAPI *GetSurfaceDesc)(void *, void *);
    /* 23 */ HRESULT (WINAPI *Initialize)(void *, void *, void *);
    /* 24 */ HRESULT (WINAPI *IsLost)(void *);
    /* 25 */ HRESULT (WINAPI *Lock)(void *, void *, void *, DWORD, void *);
    /* 26 */ HRESULT (WINAPI *ReleaseDC)(void *, void *);
    /* 27 */ HRESULT (WINAPI *Restore)(void *);
    /* 28 */ HRESULT (WINAPI *SetClipper)(void *, void *);
    /* 29 */ HRESULT (WINAPI *SetColorKey)(void *, DWORD, void *);
    /* 30 */ HRESULT (WINAPI *SetOverlayPosition)(void *, LONG, LONG);
    /* 31 */ HRESULT (WINAPI *SetPalette)(void *, void *);
    /* 32 */ HRESULT (WINAPI *Unlock)(void *, void *);
    /* 33 */ HRESULT (WINAPI *UpdateOverlay)(void *, void *, void *, void *, DWORD, void *);
    /* 34 */ HRESULT (WINAPI *UpdateOverlayDisplay)(void *, DWORD);
    /* 35 */ HRESULT (WINAPI *UpdateOverlayZOrder)(void *, DWORD, void *);
} IDirectDrawSurfaceVtbl;

/* IDirectDrawPaletteVtbl — must match the host vtable layout exactly */
typedef struct _IDirectDrawPaletteVtbl {
    /*  0 */ HRESULT (WINAPI *QueryInterface)(void *, const void *, void **);
    /*  1 */ ULONG (WINAPI *AddRef)(void *);
    /*  2 */ ULONG (WINAPI *Release)(void *);
    /*  3 */ HRESULT (WINAPI *GetCaps)(void *, DWORD *);
    /*  4 */ HRESULT (WINAPI *GetEntries)(void *, void *, DWORD, DWORD, void *);
    /*  5 */ HRESULT (WINAPI *Initialize)(void *, void *, DWORD, void *);
    /*  6 */ HRESULT (WINAPI *SetEntries)(void *, void *, DWORD, DWORD, void *);
} IDirectDrawPaletteVtbl;

struct _IDirectDraw { IDirectDrawVtbl *lpVtbl; };
struct _IDirectDrawSurface { IDirectDrawSurfaceVtbl *lpVtbl; };
struct _IDirectDrawPalette { IDirectDrawPaletteVtbl *lpVtbl; };

typedef struct _DDPALETTEENTRY {
    BYTE peRed;
    BYTE peGreen;
    BYTE peBlue;
    BYTE peFlags;
} DDPALETTEENTRY;

__declspec(dllimport) HRESULT WINAPI DirectDrawCreate(const DDGUID *guid,
                                                      IDirectDraw **dd,
                                                      void *unk);

#define DD_OK 0x00000000L
#define DDERR_INVALIDPARAMS 0x8876000DL
#define DDERR_CANTLOCKSURFACE 0x88760104L

#define DDPCAPS_8BIT 0x00000004L
#define DDPCAPS_INITIALIZE 0x00000008L

#define DDSD_CAPS 0x00000001L
#define DDSD_HEIGHT 0x00000002L
#define DDSD_WIDTH 0x00000004L
#define DDSD_BACKBUFFERCOUNT 0x00000020L

#define DDSCAPS_PRIMARYSURFACE 0x00000200L
#define DDSCAPS_BACKBUFFER 0x00000004L
#define DDSCAPS_FLIP 0x00000010L
#define DDSCAPS_COMPLEX 0x00000008L
#define DDSCAPS_OFFSCREENPLAIN 0x00000040L

#define DDSCL_NORMAL 0x00000000L

static const char k_window_class[] = "DDrawSample64Class";
static const char k_window_title[] = "DDraw Sample";
static const char k_window_title_ok[] = "DDraw Sample OK";
static const char k_window_title_fail[] = "DDraw Sample FAIL";

static HRESULT fill_surface(IDirectDrawSurface *surf, BYTE value)
{
    DDSURFACEDESC desc;
    BYTE *pixels = NULL;

    if (!surf)
        return DDERR_INVALIDPARAMS;

    ZeroMemory(&desc, sizeof(desc));
    desc.ddSize = sizeof(desc);
    /* Host Lock takes (this, rect, ddsd, flags, event) — 5 params.
     * Pixel data is returned in ddsd->lpSurface field. */
    if (surf->lpVtbl->Lock(surf, NULL, &desc, 0, NULL) != DD_OK)
        return DDERR_CANTLOCKSURFACE;

    pixels = (BYTE *)(uintptr_t)desc.lpSurface;
    if (pixels) {
        DWORD y;
        for (y = 0; y < 200; y++)
            memset(pixels + y * desc.lPitch, value, 320);
    }

    return surf->lpVtbl->Unlock(surf, NULL);
}

static HRESULT run_ddraw(HWND hwnd)
{
    IDirectDraw *dd = NULL;
    IDirectDrawSurface *primary = NULL;
    IDirectDrawSurface *backbuffer = NULL;
    IDirectDrawSurface *offscreen = NULL;
    IDirectDrawPalette *palette = NULL;
    IDirectDrawPalette *palette_from_surface = NULL;
    DDPALETTEENTRY entries[256];
    DDPALETTEENTRY readback[256];
    DDSURFACEDESC desc;
    DWORD caps;
    HRESULT hr;
    int i;

    hr = DirectDrawCreate(NULL, &dd, NULL);
    if (hr != DD_OK)
        goto cleanup;

    hr = dd->lpVtbl->SetCooperativeLevel(dd, (void *)(uintptr_t)hwnd, DDSCL_NORMAL);
    if (hr != DD_OK)
        goto cleanup;

    hr = dd->lpVtbl->SetDisplayMode(dd, 320, 200, 8);
    if (hr != DD_OK)
        goto cleanup;

    for (i = 0; i < 256; i++) {
        entries[i].peRed = (BYTE)i;
        entries[i].peGreen = (BYTE)(255 - i);
        entries[i].peBlue = (BYTE)(i / 2);
        entries[i].peFlags = 0;
    }

    hr = dd->lpVtbl->CreatePalette(dd, DDPCAPS_8BIT | DDPCAPS_INITIALIZE,
                                   entries, (void **)&palette, NULL);
    if (hr != DD_OK)
        goto cleanup;

    ZeroMemory(readback, sizeof(readback));
    hr = palette->lpVtbl->GetEntries(palette, NULL, 0, 256, readback);
    if (hr != DD_OK)
        goto cleanup;

    entries[7].peRed = 0x11;
    entries[7].peGreen = 0x33;
    entries[7].peBlue = 0x55;
    hr = palette->lpVtbl->SetEntries(palette, NULL, 7, 1, &entries[7]);
    if (hr != DD_OK)
        goto cleanup;

    ZeroMemory(&desc, sizeof(desc));
    desc.ddSize = sizeof(desc);
    desc.ddFlags = DDSD_CAPS | DDSD_WIDTH | DDSD_HEIGHT | DDSD_BACKBUFFERCOUNT;
    desc.ddCaps = DDSCAPS_PRIMARYSURFACE | DDSCAPS_FLIP | DDSCAPS_COMPLEX;
    desc.lWidth = 320;
    desc.lHeight = 200;
    desc.dwBackBufferCount = 1;

    hr = dd->lpVtbl->CreateSurface(dd, &desc, (void **)&primary, NULL);
    if (hr != DD_OK)
        goto cleanup;

    hr = primary->lpVtbl->SetPalette(primary, palette);
    if (hr != DD_OK)
        goto cleanup;

    hr = primary->lpVtbl->GetPalette(primary, (void **)&palette_from_surface);
    if (hr != DD_OK)
        goto cleanup;

    caps = DDSCAPS_BACKBUFFER;
    hr = primary->lpVtbl->GetAttachedSurface(primary, &caps, (void **)&backbuffer);
    if (hr != DD_OK)
        goto cleanup;

    ZeroMemory(&desc, sizeof(desc));
    desc.ddSize = sizeof(desc);
    desc.ddFlags = DDSD_CAPS | DDSD_WIDTH | DDSD_HEIGHT;
    desc.ddCaps = DDSCAPS_OFFSCREENPLAIN;
    desc.lWidth = 320;
    desc.lHeight = 200;

    hr = dd->lpVtbl->CreateSurface(dd, &desc, (void **)&offscreen, NULL);
    if (hr != DD_OK)
        goto cleanup;

    hr = offscreen->lpVtbl->SetPalette(offscreen, palette);
    if (hr != DD_OK)
        goto cleanup;

    hr = fill_surface(offscreen, 0x2A);
    if (hr != DD_OK)
        goto cleanup;

    hr = backbuffer->lpVtbl->Blt(backbuffer, NULL, offscreen, NULL, 0, NULL);
    if (hr != DD_OK)
        goto cleanup;

    hr = backbuffer->lpVtbl->BltFast(backbuffer, 0, 0, offscreen, NULL, 0);
    if (hr != DD_OK)
        goto cleanup;

    hr = primary->lpVtbl->Flip(primary, NULL, 0);

cleanup:
    if (offscreen)
        offscreen->lpVtbl->Release(offscreen);
    if (backbuffer)
        backbuffer->lpVtbl->Release(backbuffer);
    if (primary)
        primary->lpVtbl->Release(primary);
    if (palette_from_surface)
        palette_from_surface->lpVtbl->Release(palette_from_surface);
    if (palette)
        palette->lpVtbl->Release(palette);
    if (dd)
        dd->lpVtbl->Release(dd);
    return hr;
}

static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    (void)wParam;
    (void)lParam;

    switch (msg) {
    case WM_CLOSE:
        DestroyWindow(hwnd);
        return 0;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    default:
        return DefWindowProcA(hwnd, msg, wParam, lParam);
    }
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance,
                   LPSTR lpCmdLine, int nCmdShow)
{
    WNDCLASSA wc;
    HWND hwnd;
    MSG msg;
    HRESULT hr;

    (void)hPrevInstance;
    (void)lpCmdLine;
    (void)nCmdShow;

    ZeroMemory(&wc, sizeof(wc));
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = k_window_class;

    if (!RegisterClassA(&wc))
        return 1;

    hwnd = CreateWindowExA(0, k_window_class, k_window_title,
                           WS_VISIBLE | WS_OVERLAPPEDWINDOW,
                           0, 0, 320, 200, NULL, NULL, hInstance, NULL);
    if (!hwnd)
        return 1;

    hr = run_ddraw(hwnd);
    SetWindowTextA(hwnd, hr == DD_OK ? k_window_title_ok : k_window_title_fail);

    while (GetMessageA(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageA(&msg);
    }

    return hr == DD_OK ? 0 : 2;
}
