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

typedef struct _IDirectDrawVtbl {
    HRESULT (WINAPI *QueryInterface)(IDirectDraw *, const DDGUID *, void **);
    ULONG (WINAPI *AddRef)(IDirectDraw *);
    ULONG (WINAPI *Release)(IDirectDraw *);
    HRESULT (WINAPI *Compact)(IDirectDraw *);
    HRESULT (WINAPI *GetMonitorHandle)(IDirectDraw *, void *);
    HRESULT (WINAPI *GetAvailableVidMem)(IDirectDraw *, void *);
    HRESULT (WINAPI *GetMonitorFrequency)(IDirectDraw *, DWORD *);
    HRESULT (WINAPI *GetFourCCCodes)(IDirectDraw *, DWORD *);
    HRESULT (WINAPI *GetSurfaceFromDC)(IDirectDraw *, void *, void **);
    HRESULT (WINAPI *EnumDisplayModes)(DWORD, void *, void *, void *);
    HRESULT (WINAPI *GetDisplayMode)(IDirectDraw *, void *);
    HRESULT (WINAPI *RestoreDisplayMode)(IDirectDraw *);
    HRESULT (WINAPI *RestoreAllSurfaces)(IDirectDraw *);
    HRESULT (WINAPI *SetCooperativeLevel)(IDirectDraw *, void *, DWORD);
    HRESULT (WINAPI *SetDisplayMode)(IDirectDraw *, DWORD, DWORD, DWORD);
    HRESULT (WINAPI *CreateSurface)(IDirectDraw *, void *, void **, void *);
    HRESULT (WINAPI *GetDC)(IDirectDraw *, void *);
    HRESULT (WINAPI *ReleaseDC)(IDirectDraw *, void *);
    HRESULT (WINAPI *CreatePalette)(IDirectDraw *, DWORD, void *, void **, void *);
} IDirectDrawVtbl;

typedef struct _IDirectDrawSurfaceVtbl {
    HRESULT (WINAPI *QueryInterface)(IDirectDrawSurface *, const DDGUID *, void **);
    ULONG (WINAPI *AddRef)(IDirectDrawSurface *);
    ULONG (WINAPI *Release)(IDirectDrawSurface *);
    HRESULT (WINAPI *AddAttachedSurface)(IDirectDrawSurface *, void *);
    HRESULT (WINAPI *Blt)(IDirectDrawSurface *, void *, void *, void *, DWORD, void *);
    HRESULT (WINAPI *BltBatch)(IDirectDrawSurface *, void *, DWORD, DWORD);
    HRESULT (WINAPI *BltFast)(IDirectDrawSurface *, DWORD, DWORD, void *, void *, DWORD);
    HRESULT (WINAPI *DeleteAttachedSurface)(IDirectDrawSurface *, DWORD, void *);
    HRESULT (WINAPI *Flip)(IDirectDrawSurface *, void *, DWORD);
    HRESULT (WINAPI *GetAttachedSurface)(IDirectDrawSurface *, void *, void **);
    HRESULT (WINAPI *GetBltStatus)(IDirectDrawSurface *, DWORD, DWORD);
    HRESULT (WINAPI *GetDC)(IDirectDrawSurface *, void **);
    HRESULT (WINAPI *GetFlipStatus)(IDirectDrawSurface *, DWORD, DWORD);
    HRESULT (WINAPI *GetOverlayPosition)(IDirectDrawSurface *, LONG *, LONG *);
    HRESULT (WINAPI *GetPalette)(IDirectDrawSurface *, void **);
    HRESULT (WINAPI *GetSurfaceDesc)(IDirectDrawSurface *, void *);
    HRESULT (WINAPI *IsLost)(IDirectDrawSurface *);
    HRESULT (WINAPI *Lock)(IDirectDrawSurface *, void *, void **, void *, DWORD, void *);
    HRESULT (WINAPI *ReleaseDC)(IDirectDrawSurface *, void *);
    HRESULT (WINAPI *Restore)(IDirectDrawSurface *);
    HRESULT (WINAPI *SetClipper)(IDirectDrawSurface *, void *);
    HRESULT (WINAPI *SetColorKey)(IDirectDrawSurface *, DWORD, void *);
    HRESULT (WINAPI *SetOverlayPosition)(IDirectDrawSurface *, LONG, LONG);
    HRESULT (WINAPI *SetPalette)(IDirectDrawSurface *, void *);
    HRESULT (WINAPI *Unlock)(IDirectDrawSurface *, void *);
} IDirectDrawSurfaceVtbl;

typedef struct _IDirectDrawPaletteVtbl {
    HRESULT (WINAPI *QueryInterface)(IDirectDrawPalette *, const DDGUID *, void **);
    ULONG (WINAPI *AddRef)(IDirectDrawPalette *);
    ULONG (WINAPI *Release)(IDirectDrawPalette *);
    HRESULT (WINAPI *GetEntries)(IDirectDrawPalette *, void *, DWORD, DWORD, void *);
    HRESULT (WINAPI *SetEntries)(IDirectDrawPalette *, void *, DWORD, DWORD, void *);
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

#define DDPCAPS_8BIT 0x00000001L
#define DDPCAPS_INITIALIZE 0x00000008L

#define DDSD_CAPS 0x00000001L
#define DDSD_HEIGHT 0x00000002L
#define DDSD_WIDTH 0x00000004L
#define DDSD_BACKBUFFERCOUNT 0x00000020L

#define DDSCAPS_PRIMARYSURFACE 0x00000001L
#define DDSCAPS_BACKBUFFER 0x00000002L
#define DDSCAPS_FLIP 0x00000004L
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
    if (surf->lpVtbl->Lock(surf, NULL, (void **)&pixels, &desc, 0, NULL) != DD_OK)
        return DDERR_CANTLOCKSURFACE;

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
