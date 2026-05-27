/*
 * ddraw_interface.c
 *
 * Minimal maintained DirectDraw path for Doom95:
 * - DirectDrawCreate + IDirectDraw core methods
 * - primary/backbuffer surface creation
 * - palette/clipper creation entrypoints
 * - cooperative-level, display-mode, and surface-create orchestration
 */

#include "ddraw_priv.h"
#include "user32_priv.h"

extern rb_window_t rb_window_create(const char *title, int x, int y, int w, int h,
                                    uint32_t flags) __attribute__((weak));
extern int rb_window_set_fullscreen(rb_window_t win, int fullscreen,
                                    int w, int h, int bpp) __attribute__((weak));

void ddraw_debug_counter(const char *tag)
{
    typedef struct {
        const char *tag;
        uint32_t count;
    } ddraw_debug_counter_slot;
    static ddraw_debug_counter_slot slots[16];
    int i;

    if (!debug_level_at_least(1) || !tag)
        return;

    for (i = 0; i < 16; i++) {
        if (slots[i].tag == tag || slots[i].tag == NULL) {
            uint32_t count;

            if (slots[i].tag == NULL)
                slots[i].tag = tag;
            count = ++slots[i].count;
            if ((count & (count - 1)) == 0 || (count % 100000u) == 0)
                DEBUG("ddraw: %s count=%u", tag, count);
            return;
        }
    }
}

/*
 * ddraw_guest_ptr_valid — check that a guest pointer is in the valid
 * guest address range (> 0x10000).  Prevents writes to low addresses
 * that could alias stack slots or other host memory.
 */
static inline int ddraw_guest_ptr_valid(const void *ptr)
{
    return (uintptr_t)ptr > 0x10000;
}

static HRESULT KERNEL32_STUB ddraw_DuplicateSurface(void *this_ptr, void *lpDDSurface,
                                                    void **lplpDupDDSurface)
{
    (void)this_ptr;
    (void)lpDDSurface;
    (void)lplpDupDDSurface;
    return DDERR_UNSUPPORTED;
}

static HRESULT KERNEL32_STUB ddraw_QueryInterface(void *this_ptr, const GUID *riid, void **ppvObj)
{
    return ddraw_query_interface((my_dd_t *)this_ptr, riid, ppvObj);
}

static uint32_t KERNEL32_STUB ddraw_AddRef(void *this_ptr)
{
    return ddraw_add_ref((my_dd_t *)this_ptr);
}

static uint32_t KERNEL32_STUB ddraw_Release(void *this_ptr)
{
    return ddraw_release((my_dd_t *)this_ptr);
}

static HRESULT KERNEL32_STUB ddraw_Compact(void *this_ptr) { (void)this_ptr; return DDERR_UNSUPPORTED; }
static HRESULT KERNEL32_STUB ddraw_CreateClipper(void *this_ptr, uint32_t flags, void **lplpClipper, void *unk)
{
    my_dd_t *dd = (my_dd_t *)this_ptr;

    (void)flags;
    (void)unk;

    return ddraw_clipper_create(dd, flags, lplpClipper);
}
static HRESULT KERNEL32_STUB ddraw_FlipToGDISurface(void *this_ptr) { (void)this_ptr; return DDERR_UNSUPPORTED; }
static HRESULT KERNEL32_STUB ddraw_WaitForVerticalBlank(void *this_ptr, uint32_t flags, void *hEvent)
{
    (void)this_ptr;
    (void)flags;
    (void)hEvent;

    return ddraw_wait_for_vertical_blank();
}

static HRESULT KERNEL32_STUB ddraw_GetMonitorFrequency(void *this_ptr, uint32_t *dwFreq)
{
    (void)this_ptr;
    return ddraw_get_monitor_frequency(dwFreq);
}

static HRESULT KERNEL32_STUB ddraw_GetScanLine(void *this_ptr, uint32_t *dwScanLine)
{
    (void)this_ptr;
    return ddraw_get_scan_line(dwScanLine);
}

static HRESULT KERNEL32_STUB ddraw_GetVerticalBlankStatus(void *this_ptr, int *lpInVerticalBlank)
{
    (void)this_ptr;
    return ddraw_get_vertical_blank_status(lpInVerticalBlank);
}

static HRESULT KERNEL32_STUB ddraw_GetFourCCCodes(void *this_ptr, uint32_t *lpNumCodes,
                                                  uint32_t *lpCodes)
{
    (void)this_ptr;
    return ddraw_get_fourcc_codes(lpNumCodes, lpCodes);
}

static HRESULT KERNEL32_STUB ddraw_GetGDISurface(void *this_ptr, void **lpSurface)
{
    my_dd_t *dd = (my_dd_t *)this_ptr;

    return ddraw_get_gdi_surface(dd, lpSurface);
}

static HRESULT KERNEL32_STUB ddraw_Initialize(void *this_ptr, GUID *lpGUID)
{
    (void)this_ptr;
    if (lpGUID && !ddraw_guest_ptr_valid(lpGUID))
        return DDERR_INVALIDPARAMS;
    (void)lpGUID;
    return DD_OK;
}

static HRESULT KERNEL32_STUB ddraw_EnumDisplayModes(void *this_ptr, uint32_t dwFlags, void *ddsd,
                                                    void *lpContext, void *lpEnumCallback)
{
    (void)this_ptr;
    (void)dwFlags;
    (void)ddsd;
    (void)lpContext;
    (void)lpEnumCallback;
    return DDERR_UNSUPPORTED;
}

static HRESULT KERNEL32_STUB ddraw_EnumSurfaces(void *this_ptr, uint32_t dwFlags, void *ddsd,
                                                void *lpContext, void *lpEnumCallback)
{
    (void)this_ptr;
    (void)dwFlags;
    (void)ddsd;
    (void)lpContext;
    (void)lpEnumCallback;
    return DDERR_UNSUPPORTED;
}

static HRESULT KERNEL32_STUB ddraw_GetDisplayMode(void *this_ptr, void *ddsd)
{
    my_dd_t *dd = (my_dd_t *)this_ptr;

    return ddraw_get_display_mode(dd, ddsd);
}

static HRESULT KERNEL32_STUB ddraw_RestoreDisplayMode(void *this_ptr)
{
    (void)this_ptr;
    return ddraw_restore_display_mode();
}

static HRESULT KERNEL32_STUB ddraw_SetCooperativeLevel(void *this_ptr, void *hwnd, uint32_t flags)
{
    my_dd_t *dd = (my_dd_t *)this_ptr;
    wine_window_entry *entry;

    if (!dd)
        return DDERR_INVALIDPARAMS;

    dd->cooperative_hwnd = hwnd;
    dd->cooperative_level = flags;
    dd->is_exclusive = (flags & (DDSCL_FULLSCREEN | DDSCL_EXCLUSIVE)) ? 1 : 0;

    entry = get_window_entry((HWND)hwnd);
    if (entry)
        dd->rb_window = entry->sdl_window;

    DEBUG("ddraw: SetCooperativeLevel hwnd=%p flags=0x%x rb_window=%u", hwnd, flags,
          (unsigned int)dd->rb_window);
    return DD_OK;
}

static HRESULT KERNEL32_STUB ddraw_SetDisplayMode(void *this_ptr, uint32_t width,
                                                  uint32_t height, uint32_t bpp)
{
    my_dd_t *dd = (my_dd_t *)this_ptr;

    if (!dd)
        return DDERR_INVALIDPARAMS;
    if (width == 0 || height == 0 || width > 8192 || height > 8192 || bpp > 32)
        return DDERR_UNSUPPORTEDMODE;
    if (!dd->cooperative_hwnd)
        dd->cooperative_level = DDSCL_NORMAL;
    if (!ddraw_ensure_backend())
        return DDERR_UNSUPPORTED;

    dd->current_mode_w = width;
    dd->current_mode_h = height;
    dd->current_mode_bpp = bpp;

    if (!dd->rb_window && rb_window_create) {
        dd->rb_window = rb_window_create("DirectDraw", -1, -1,
                                         (int)width, (int)height, 0);
        if (dd->rb_window)
            dd->owns_window = 1;
    }

    if (dd->rb_window && rb_window_set_fullscreen)
        rb_window_set_fullscreen(dd->rb_window, dd->is_exclusive,
                                 (int)width, (int)height, (int)bpp);

    DEBUG("ddraw: SetDisplayMode %ux%ux%u exclusive=%d rb_window=%u", width, height, bpp,
          dd->is_exclusive, (unsigned int)dd->rb_window);
    return DD_OK;
}

static HRESULT KERNEL32_STUB ddraw_CreateSurface(void *this_ptr, void *ddsd,
                                                 void **lplpDDSurface, void *unk)
{
    my_dd_t *dd = (my_dd_t *)this_ptr;
    uint32_t caps = 0;
    uint32_t width = 0;
    uint32_t height = 0;
    uint32_t backbuffers = 0;

    (void)unk;

    if (!dd || !ddsd || !lplpDDSurface)
        return DDERR_INVALIDPARAMS;
    if (!ddraw_ensure_backend())
        return DDERR_UNSUPPORTED;

    ddraw_parse_surface_desc(ddsd, &caps, &width, &height, &backbuffers);

    if (width == 0)
        width = dd->current_mode_w;
    if (height == 0)
        height = dd->current_mode_h;

    if ((caps & DDSCAPS_PRIMARYSURFACE) && !dd->rb_window)
        return DDERR_NOCOOPERATIVELEVELSET;

    DEBUG("ddraw: CreateSurface caps=0x%x size=%ux%u backbuffers=%u",
          caps, width, height, backbuffers);

    if ((caps & DDSCAPS_PRIMARYSURFACE) && (caps & DDSCAPS_FLIP)) {
        my_surface_t *primary = NULL;
        my_surface_t *backbuffer = NULL;
        HRESULT hr;

        hr = ddraw_create_flip_chain_surface(dd, width, height, backbuffers,
                                             caps, &primary, &backbuffer, lplpDDSurface);
        if (hr != DD_OK)
            return hr;

        primary->width = width;
        primary->height = height;
        primary->pitch = (int32_t)width;
        if (backbuffer) {
            backbuffer->width = width;
            backbuffer->height = height;
            backbuffer->pitch = (int32_t)width;
        }
    } else {
        my_surface_t *primary = NULL;
        HRESULT hr;

        hr = ddraw_create_regular_surface(dd, width, height, caps,
                                          &primary, lplpDDSurface);
        if (hr != DD_OK)
            return hr;

        primary->width = width;
        primary->height = height;
        primary->pitch = (int32_t)width;
    }

    return DD_OK;
}

static HRESULT KERNEL32_STUB ddraw_CreatePalette(void *this_ptr, uint32_t flags,
                                                 void *ddpalette, void **lplpDDPalette,
                                                 void *unk)
{
    my_dd_t *dd = (my_dd_t *)this_ptr;

    (void)unk;

    if (!dd || !lplpDDPalette)
        return DDERR_INVALIDPARAMS;
    if (!ddraw_ensure_backend())
        return DDERR_UNSUPPORTED;
    return ddraw_palette_create(dd, flags, ddpalette, lplpDDPalette);
}

static HRESULT KERNEL32_STUB ddraw_GetCaps(void *this_ptr, void *ddcaps1, void *ddcaps2)
{
    my_dd_t *dd = (my_dd_t *)this_ptr;

    return ddraw_get_caps(dd, ddcaps1, ddcaps2);
}

const IDirectDrawVtbl ddraw_vtbl = {
    .QueryInterface = ddraw_QueryInterface,
    .AddRef = ddraw_AddRef,
    .Release = ddraw_Release,
    .Compact = ddraw_Compact,
    .CreateClipper = ddraw_CreateClipper,
    .CreatePalette = ddraw_CreatePalette,
    .CreateSurface = ddraw_CreateSurface,
    .DuplicateSurface = ddraw_DuplicateSurface,
    .EnumDisplayModes = ddraw_EnumDisplayModes,
    .EnumSurfaces = ddraw_EnumSurfaces,
    .FlipToGDISurface = ddraw_FlipToGDISurface,
    .GetCaps = ddraw_GetCaps,
    .GetDisplayMode = ddraw_GetDisplayMode,
    .GetFourCCCodes = ddraw_GetFourCCCodes,
    .GetGDISurface = ddraw_GetGDISurface,
    .GetMonitorFrequency = ddraw_GetMonitorFrequency,
    .GetScanLine = ddraw_GetScanLine,
    .GetVerticalBlankStatus = ddraw_GetVerticalBlankStatus,
    .Initialize = ddraw_Initialize,
    .RestoreDisplayMode = ddraw_RestoreDisplayMode,
    .SetCooperativeLevel = ddraw_SetCooperativeLevel,
    .SetDisplayMode = ddraw_SetDisplayMode,
    .WaitForVerticalBlank = ddraw_WaitForVerticalBlank,
};

HRESULT KERNEL32_STUB DirectDrawCreate(const GUID *lpGUID, LPDIRECTDRAW *lplpDD,
                                       void *pUnkOuter)
{
    my_dd_t *dd;

    (void)lpGUID;

    if (!lplpDD)
        return DDERR_INVALIDPARAMS;
    if (pUnkOuter)
        return CLASS_E_NOAGGREGATION;

    dd = ddraw_create_instance();
    if (!dd)
        return DDERR_OUTOFMEMORY;
    *lplpDD = FORCE_PTR_RETURN(dd);
    DEBUG("ddraw: DirectDrawCreate guid=%p -> %p", lpGUID, dd);
    return DD_OK;
}
