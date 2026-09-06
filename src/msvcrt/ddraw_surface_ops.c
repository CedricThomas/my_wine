/*
 * ddraw_surface_ops.c
 *
 * Internal DirectDraw surface behavior support:
 * - IDirectDrawSurface COM behavior beyond lifetime
 * - rect conversion and backend blit/flip/lock/unlock paths
 * - palette and attached-surface accessors
 */

#include "ddraw_priv.h"

extern int rb_surface_lock(rb_surface_t surf, const rb_rect_t *rect,
                           uint8_t **out_data, int *out_pitch) __attribute__((weak));
extern int rb_surface_unlock(rb_surface_t surf) __attribute__((weak));
extern int rb_surface_blt(rb_surface_t dst, const rb_rect_t *dst_rect,
                          rb_surface_t src, const rb_rect_t *src_rect,
                          uint32_t color, uint32_t flags) __attribute__((weak));
extern int rb_surface_flip(rb_surface_t surf) __attribute__((weak));
extern int rb_surface_set_palette(rb_surface_t surf, rb_palette_t pal) __attribute__((weak));

static HRESULT KERNEL32_STUB surface_QueryInterface(void *this_ptr, const GUID *riid,
                                                    void **ppvObj)
{
    my_surface_t *surf = (my_surface_t *)this_ptr;

    if (!surf || !riid || !ppvObj)
        return DDERR_INVALIDPARAMS;

    *ppvObj = NULL;
    if (IsEqualGUID(riid, &IID_IDirectDrawSurface)) {
        ddraw_surface_add_ref(surf);
        *ppvObj = FORCE_PTR_RETURN(surf);
        return DD_OK;
    }
    return DDERR_UNSUPPORTED;
}

static uint32_t KERNEL32_STUB surface_AddRef(void *this_ptr)
{
    return ddraw_surface_add_ref((my_surface_t *)this_ptr);
}

static uint32_t KERNEL32_STUB surface_Release(void *this_ptr)
{
    return ddraw_surface_release((my_surface_t *)this_ptr);
}

static HRESULT KERNEL32_STUB surface_AddAttachedSurface(void *this_ptr, void *lpDDS)
{
    my_surface_t *surf = (my_surface_t *)this_ptr;
    my_surface_t *attached = (my_surface_t *)lpDDS;

    if (!surf || !attached)
        return DDERR_INVALIDPARAMS;
    if (surf->next)
        return DDERR_SURFACEALREADYATTACHED;

    ddraw_surface_add_ref(attached);
    surf->next = attached;
    return DD_OK;
}

static HRESULT KERNEL32_STUB surface_AddOverlayDirtyRect(void *this_ptr, void *lpDDRect)
{
    (void)this_ptr;
    (void)lpDDRect;
    return DDERR_UNSUPPORTED;
}

static int ddraw_rect_to_rb(const DDRECT *src, rb_rect_t *dst)
{
    if (!src || !dst)
        return 0;
    dst->x = (int32_t)src->left;
    dst->y = (int32_t)src->top;
    dst->w = (int32_t)(src->right - src->left);
    dst->h = (int32_t)(src->bottom - src->top);
    return 1;
}

static HRESULT KERNEL32_STUB surface_Blt(void *this_ptr, void *lpDestRect,
                                         void *lpDDSrcSurface, void *lpSrcRect,
                                         uint32_t dwFlags, void *lpDDBltFX)
{
    my_surface_t *dst = (my_surface_t *)this_ptr;
    my_surface_t *src = (my_surface_t *)lpDDSrcSurface;
    rb_rect_t dst_rect;
    rb_rect_t src_rect;
    const rb_rect_t *dst_rect_ptr = NULL;
    const rb_rect_t *src_rect_ptr = NULL;
    uint32_t color = 0;
    uint32_t rb_flags = 0;

    if (!dst || !rb_surface_blt)
        return DDERR_INVALIDPARAMS;
    ddraw_debug_counter("Blt");

    if (lpDestRect && ddraw_rect_to_rb((const DDRECT *)lpDestRect, &dst_rect))
        dst_rect_ptr = &dst_rect;
    if (lpSrcRect && ddraw_rect_to_rb((const DDRECT *)lpSrcRect, &src_rect))
        src_rect_ptr = &src_rect;

    if (dwFlags & DDBLT_COLORFILL) {
        const DDBLTFX *fx = (const DDBLTFX *)lpDDBltFX;
        rb_flags |= RB_BLT_COLORFILL;
        if (fx)
            color = fx->pDDDestRGB;
    } else {
        rb_flags |= RB_BLT_SRCCOPY;
    }

    if (rb_surface_blt(dst->rb_surface, dst_rect_ptr,
                       src ? src->rb_surface : 0, src_rect_ptr,
                       color, rb_flags) != RB_OK)
        return DDERR_UNSUPPORTED;
    return DD_OK;
}

static HRESULT KERNEL32_STUB surface_BltBatch(void *this_ptr, void *lpDDLBltData,
                                              uint32_t dwCount, uint32_t dwFlags)
{
    (void)this_ptr;
    (void)lpDDLBltData;
    (void)dwCount;
    (void)dwFlags;
    return DDERR_UNSUPPORTED;
}

static HRESULT KERNEL32_STUB surface_BltFast(void *this_ptr, uint32_t dwX, uint32_t dwY,
                                             void *lpDDSrcSurface, void *lpSrcRect,
                                             uint32_t dwFlags)
{
    DDRECT dst_rect;
    DDRECT src_rect;
    my_surface_t *src = (my_surface_t *)lpDDSrcSurface;

    (void)dwFlags;

    if (!src)
        return DDERR_INVALIDPARAMS;
    ddraw_debug_counter("BltFast");

    if (lpSrcRect) {
        src_rect = *(const DDRECT *)lpSrcRect;
    } else {
        src_rect.left = 0;
        src_rect.top = 0;
        src_rect.right = src->width;
        src_rect.bottom = src->height;
    }

    dst_rect.left = dwX;
    dst_rect.top = dwY;
    dst_rect.right = dwX + (src_rect.right - src_rect.left);
    dst_rect.bottom = dwY + (src_rect.bottom - src_rect.top);

    return surface_Blt(this_ptr, &dst_rect, lpDDSrcSurface, &src_rect, 0, NULL);
}

static HRESULT KERNEL32_STUB surface_DeleteAttachedSurface(void *this_ptr, uint32_t dwFlags,
                                                           void *lpDDS)
{
    my_surface_t *surf = (my_surface_t *)this_ptr;
    my_surface_t *attached = (my_surface_t *)lpDDS;

    (void)dwFlags;

    if (!surf || !attached || surf->next != attached)
        return DDERR_INVALIDPARAMS;

    surf->next = NULL;
    ddraw_surface_release(attached);
    return DD_OK;
}

static HRESULT KERNEL32_STUB surface_EnumAttachedSurfaces(void *this_ptr, void *lpContext,
                                                          void *lpEnumCallback)
{
    (void)this_ptr;
    (void)lpContext;
    (void)lpEnumCallback;
    return DDERR_UNSUPPORTED;
}

static HRESULT KERNEL32_STUB surface_EnumOverlayZOrders(void *this_ptr, uint32_t dwFlags,
                                                        void *lpContext, void *lpEnumCallback)
{
    (void)this_ptr;
    (void)dwFlags;
    (void)lpContext;
    (void)lpEnumCallback;
    return DDERR_UNSUPPORTED;
}

static HRESULT KERNEL32_STUB surface_Flip(void *this_ptr, void *lpDDSurface, uint32_t dwFlags)
{
    my_surface_t *surf = (my_surface_t *)this_ptr;

    (void)lpDDSurface;
    (void)dwFlags;

    if (!surf || !rb_surface_flip)
        return DDERR_INVALIDPARAMS;
    ddraw_debug_counter("Flip");
    return (rb_surface_flip(surf->rb_surface) == RB_OK) ? DD_OK : DDERR_NOFLIP;
}

static HRESULT KERNEL32_STUB surface_GetAttachedSurface(void *this_ptr, void *lpDDSCaps,
                                                        void **lppDDSSurface)
{
    my_surface_t *surf = (my_surface_t *)this_ptr;
    const uint32_t caps = lpDDSCaps ? *(const uint32_t *)lpDDSCaps : 0;

    if (!surf || !lppDDSSurface)
        return DDERR_INVALIDPARAMS;

    *lppDDSSurface = NULL;
    if ((caps & DDSCAPS_BACKBUFFER) && surf->next) {
        ddraw_surface_add_ref(surf->next);
        *lppDDSSurface = FORCE_PTR_RETURN(surf->next);
        ddraw_debug_counter("GetAttachedSurface");
        return DD_OK;
    }

    return DDERR_NOTFOUND;
}

static HRESULT KERNEL32_STUB surface_GetBltStatus(void *this_ptr, uint32_t dwFlags)
{
    (void)this_ptr;
    (void)dwFlags;
    ddraw_debug_counter("GetBltStatus");
    return DD_OK;
}

static HRESULT KERNEL32_STUB surface_GetCaps(void *this_ptr, void *lpDDSCaps)
{
    my_surface_t *surf = (my_surface_t *)this_ptr;
    uint32_t *caps = (uint32_t *)lpDDSCaps;

    if (!surf || !caps)
        return DDERR_INVALIDPARAMS;

    *caps = surf->caps;
    return DD_OK;
}

static HRESULT KERNEL32_STUB surface_GetColorKey(void *this_ptr, uint32_t dwFlags, void *lpDDColorKey)
{
    (void)this_ptr;
    (void)dwFlags;
    (void)lpDDColorKey;
    return DDERR_UNSUPPORTED;
}

static HRESULT KERNEL32_STUB surface_GetDC(void *this_ptr, void **lphDC)
{
    (void)this_ptr;
    (void)lphDC;
    return DDERR_UNSUPPORTED;
}

static HRESULT KERNEL32_STUB surface_GetFlipStatus(void *this_ptr, uint32_t dwFlags)
{
    (void)this_ptr;
    (void)dwFlags;
    ddraw_debug_counter("GetFlipStatus");
    return DD_OK;
}

static HRESULT KERNEL32_STUB surface_GetOverlayPosition(void *this_ptr, int32_t *lpl,
                                                        int32_t *lpt)
{
    (void)this_ptr;
    (void)lpl;
    (void)lpt;
    return DDERR_UNSUPPORTED;
}

static HRESULT KERNEL32_STUB surface_GetPalette(void *this_ptr, void **lppPalette)
{
    my_surface_t *surf = (my_surface_t *)this_ptr;

    if (!surf || !lppPalette)
        return DDERR_INVALIDPARAMS;

    *lppPalette = NULL;
    if (!surf->palette)
        return DDERR_NOTFOUND;

    ddraw_palette_add_ref(surf->palette);
    *lppPalette = FORCE_PTR_RETURN(surf->palette);
    return DD_OK;
}

static HRESULT KERNEL32_STUB surface_GetSurfaceDesc(void *this_ptr, void *lpDDSurfaceDesc)
{
    my_surface_t *surf = (my_surface_t *)this_ptr;

    if (!surf || !lpDDSurfaceDesc)
        return DDERR_INVALIDPARAMS;

    ddraw_fill_surface_desc(surf, lpDDSurfaceDesc, NULL);
    return DD_OK;
}

static HRESULT KERNEL32_STUB surface_GetPixelFormat(void *this_ptr, void *lpDDPixelFormat)
{
    (void)this_ptr;
    (void)lpDDPixelFormat;
    return DDERR_UNSUPPORTED;
}

static HRESULT KERNEL32_STUB surface_Initialize(void *this_ptr, void *lpDDraw,
                                                void *lpDDSurfaceDesc)
{
    (void)this_ptr;
    (void)lpDDraw;
    (void)lpDDSurfaceDesc;
    return DD_OK;
}

static HRESULT KERNEL32_STUB surface_IsLost(void *this_ptr)
{
    (void)this_ptr;
    return DD_OK;
}

static HRESULT KERNEL32_STUB surface_Lock(void *this_ptr, void *lpDDRect,
                                          void *lpDDSurfaceDesc, uint32_t dwFlags,
                                          void *hEvent)
{
    my_surface_t *surf = (my_surface_t *)this_ptr;
    rb_rect_t rect;
    const rb_rect_t *rect_ptr = NULL;
    uint8_t *data = NULL;
    int pitch = 0;

    (void)dwFlags;
    (void)hEvent;

    if (!surf || !rb_surface_lock)
        return DDERR_INVALIDPARAMS;
    if (surf->locked)
        return DDERR_SURFACEBUSY;

    if (lpDDRect && ddraw_rect_to_rb((const DDRECT *)lpDDRect, &rect))
        rect_ptr = &rect;

    if (rb_surface_lock(surf->rb_surface, rect_ptr, &data, &pitch) != RB_OK || !data)
        return DDERR_NOLOCKING;

    surf->locked = 1;
    surf->pitch = pitch;
    ddraw_debug_counter("Lock");
    if (lpDDSurfaceDesc)
        ddraw_fill_surface_desc(surf, lpDDSurfaceDesc, data);

    return DD_OK;
}

static HRESULT KERNEL32_STUB surface_ReleaseDC(void *this_ptr, void *hDC)
{
    (void)this_ptr;
    (void)hDC;
    return DDERR_UNSUPPORTED;
}

static HRESULT KERNEL32_STUB surface_Restore(void *this_ptr)
{
    (void)this_ptr;
    return DD_OK;
}

static HRESULT KERNEL32_STUB surface_SetClipper(void *this_ptr, void *lpDDClipper)
{
    my_surface_t *surf = (my_surface_t *)this_ptr;
    my_clipper_t *clipper = (my_clipper_t *)lpDDClipper;

    return ddraw_surface_set_clipper(surf, clipper);
}

static HRESULT KERNEL32_STUB surface_SetColorKey(void *this_ptr, uint32_t dwFlags, void *lpDDColorKey)
{
    (void)this_ptr;
    (void)dwFlags;
    (void)lpDDColorKey;
    return DDERR_UNSUPPORTED;
}

static HRESULT KERNEL32_STUB surface_SetOverlayPosition(void *this_ptr, int32_t l, int32_t t)
{
    (void)this_ptr;
    (void)l;
    (void)t;
    return DDERR_UNSUPPORTED;
}

static HRESULT KERNEL32_STUB surface_SetPalette(void *this_ptr, void *lpPalette)
{
    my_surface_t *surf = (my_surface_t *)this_ptr;
    my_palette_t *pal = (my_palette_t *)lpPalette;

    if (!surf)
        return DDERR_INVALIDPARAMS;

    if (surf->palette && surf->palette != pal)
        ddraw_palette_release(surf->palette);
    surf->palette = pal;
    if (pal) {
        ddraw_palette_add_ref(pal);
        ddraw_debug_counter("SetPalette");
        if (rb_surface_set_palette &&
            rb_surface_set_palette(surf->rb_surface, pal->rb_palette) != RB_OK)
            return DDERR_UNSUPPORTED;
        if (surf->next && rb_surface_set_palette)
            rb_surface_set_palette(surf->next->rb_surface, pal->rb_palette);
    }

    return DD_OK;
}

static HRESULT KERNEL32_STUB surface_Unlock(void *this_ptr, void *lpDDSurfaceDesc)
{
    my_surface_t *surf = (my_surface_t *)this_ptr;

    (void)lpDDSurfaceDesc;

    if (!surf || !rb_surface_unlock)
        return DDERR_INVALIDPARAMS;
    if (!surf->locked)
        return DDERR_NOTLOCKED;

    surf->locked = 0;
    ddraw_debug_counter("Unlock");
    return (rb_surface_unlock(surf->rb_surface) == RB_OK) ? DD_OK : DDERR_NOTLOCKED;
}

static HRESULT KERNEL32_STUB surface_GetClipper(void *this_ptr, void **lppDDClipper)
{
    my_surface_t *surf = (my_surface_t *)this_ptr;

    return ddraw_surface_get_clipper(surf, lppDDClipper);
}

static HRESULT KERNEL32_STUB surface_UpdateOverlay(void *this_ptr, void *lpSrcRect,
                                                   void *lpDDSDstSurface, void *lpDstRect,
                                                   uint32_t dwFlags, void *lpDDOverlayFx)
{
    (void)this_ptr;
    (void)lpSrcRect;
    (void)lpDDSDstSurface;
    (void)lpDstRect;
    (void)dwFlags;
    (void)lpDDOverlayFx;
    return DDERR_UNSUPPORTED;
}

static HRESULT KERNEL32_STUB surface_UpdateOverlayDisplay(void *this_ptr, uint32_t dwFlags)
{
    (void)this_ptr;
    (void)dwFlags;
    return DDERR_UNSUPPORTED;
}

static HRESULT KERNEL32_STUB surface_UpdateOverlayZOrder(void *this_ptr, uint32_t dwFlags,
                                                         void *lpDDSReferenceSurface)
{
    (void)this_ptr;
    (void)dwFlags;
    (void)lpDDSReferenceSurface;
    return DDERR_UNSUPPORTED;
}

const IDirectDrawSurfaceVtbl surface_vtbl = {
    .QueryInterface = surface_QueryInterface,
    .AddRef = surface_AddRef,
    .Release = surface_Release,
    .AddAttachedSurface = surface_AddAttachedSurface,
    .AddOverlayDirtyRect = surface_AddOverlayDirtyRect,
    .Blt = surface_Blt,
    .BltBatch = surface_BltBatch,
    .BltFast = surface_BltFast,
    .DeleteAttachedSurface = surface_DeleteAttachedSurface,
    .EnumAttachedSurfaces = surface_EnumAttachedSurfaces,
    .EnumOverlayZOrders = surface_EnumOverlayZOrders,
    .Flip = surface_Flip,
    .GetAttachedSurface = surface_GetAttachedSurface,
    .GetBltStatus = surface_GetBltStatus,
    .GetCaps = surface_GetCaps,
    .GetClipper = surface_GetClipper,
    .GetColorKey = surface_GetColorKey,
    .GetDC = surface_GetDC,
    .GetFlipStatus = surface_GetFlipStatus,
    .GetOverlayPosition = surface_GetOverlayPosition,
    .GetPalette = surface_GetPalette,
    .GetPixelFormat = surface_GetPixelFormat,
    .GetSurfaceDesc = surface_GetSurfaceDesc,
    .Initialize = surface_Initialize,
    .IsLost = surface_IsLost,
    .Lock = surface_Lock,
    .ReleaseDC = surface_ReleaseDC,
    .Restore = surface_Restore,
    .SetClipper = surface_SetClipper,
    .SetColorKey = surface_SetColorKey,
    .SetOverlayPosition = surface_SetOverlayPosition,
    .SetPalette = surface_SetPalette,
    .Unlock = surface_Unlock,
    .UpdateOverlay = surface_UpdateOverlay,
    .UpdateOverlayDisplay = surface_UpdateOverlayDisplay,
    .UpdateOverlayZOrder = surface_UpdateOverlayZOrder,
};
