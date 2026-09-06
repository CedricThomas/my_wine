/*
 * ddraw_clipper.c
 *
 * Internal DirectDraw clipper object support:
 * - IDirectDraw::CreateClipper object allocation
 * - IDirectDrawClipper COM lifetime and HWND attachment access
 */

#include <string.h>

#include "ddraw_priv.h"

HRESULT ddraw_clipper_create(my_dd_t *dd, uint32_t flags, void **lplpClipper)
{
    my_clipper_t *clipper;

    if (!dd || !lplpClipper)
        return DDERR_INVALIDPARAMS;

    clipper = ddraw_alloc_mem(sizeof(*clipper));
    if (!clipper)
        return DDERR_OUTOFMEMORY;

    memset(clipper, 0, sizeof(*clipper));
    clipper->lpVtbl = (IDirectDrawClipperVtbl *)&clipper_vtbl;
    clipper->ref_count = 1;

    *lplpClipper = FORCE_PTR_RETURN(clipper);
    DEBUG("ddraw: CreateClipper flags=0x%x -> %p", flags, clipper);
    return DD_OK;
}

static HRESULT KERNEL32_STUB clipper_QueryInterface(void *this_ptr, const GUID *riid,
                                                    void **ppvObj)
{
    my_clipper_t *clipper = (my_clipper_t *)this_ptr;

    if (!clipper || !riid || !ppvObj)
        return DDERR_INVALIDPARAMS;

    *ppvObj = NULL;
    if (IsEqualGUID(riid, &IID_IDirectDrawClipper)) {
        clipper->ref_count++;
        *ppvObj = FORCE_PTR_RETURN(clipper);
        return DD_OK;
    }
    return DDERR_UNSUPPORTED;
}

uint32_t KERNEL32_STUB ddraw_clipper_add_ref(my_clipper_t *clipper)
{
    if (clipper)
        clipper->ref_count++;
    return clipper ? clipper->ref_count : 0;
}

uint32_t KERNEL32_STUB ddraw_clipper_release(my_clipper_t *clipper)
{
    uint32_t ref_count;

    if (!clipper)
        return 0;

    if (clipper->ref_count > 0)
        clipper->ref_count--;
    ref_count = clipper->ref_count;
    if (ref_count == 0)
        ddraw_free(clipper);
    return ref_count;
}

void ddraw_surface_clear_clipper(my_surface_t *surf)
{
    if (!surf || !surf->clipper)
        return;

    ddraw_clipper_release(surf->clipper);
    surf->clipper = NULL;
}

HRESULT KERNEL32_STUB ddraw_surface_set_clipper(my_surface_t *surf,
                                                my_clipper_t *clipper)
{
    if (!surf)
        return DDERR_INVALIDPARAMS;
    if (clipper == surf->clipper)
        return DD_OK;

    ddraw_surface_clear_clipper(surf);
    surf->clipper = clipper;
    if (clipper)
        ddraw_clipper_add_ref(clipper);

    DEBUG("ddraw: surface SetClipper surface=%p clipper=%p", surf, clipper);
    return DD_OK;
}

HRESULT KERNEL32_STUB ddraw_surface_get_clipper(my_surface_t *surf,
                                                void **lppDDClipper)
{
    if (!surf || !lppDDClipper)
        return DDERR_INVALIDPARAMS;
    if (!surf->clipper) {
        *lppDDClipper = NULL;
        return DDERR_NOCLIPPERATTACHED;
    }

    ddraw_clipper_add_ref(surf->clipper);
    *lppDDClipper = FORCE_PTR_RETURN(surf->clipper);
    return DD_OK;
}

static uint32_t KERNEL32_STUB clipper_AddRef(void *this_ptr)
{
    return ddraw_clipper_add_ref((my_clipper_t *)this_ptr);
}

static uint32_t KERNEL32_STUB clipper_Release(void *this_ptr)
{
    return ddraw_clipper_release((my_clipper_t *)this_ptr);
}

static HRESULT KERNEL32_STUB clipper_SetHWnd(void *this_ptr, uint32_t flags,
                                             void *hWnd)
{
    my_clipper_t *clipper = (my_clipper_t *)this_ptr;

    (void)flags;

    if (!clipper)
        return DDERR_INVALIDPARAMS;

    clipper->hWnd = hWnd;
    return DD_OK;
}

static HRESULT KERNEL32_STUB clipper_GetHWnd(void *this_ptr, void **lphWnd)
{
    my_clipper_t *clipper = (my_clipper_t *)this_ptr;

    if (!clipper || !lphWnd)
        return DDERR_INVALIDPARAMS;

    *lphWnd = FORCE_PTR_RETURN(clipper->hWnd);
    return DD_OK;
}

static HRESULT KERNEL32_STUB clipper_SetClipList(void *this_ptr, void *lpClipList,
                                                 void *hWnd)
{
    (void)this_ptr;
    (void)lpClipList;
    (void)hWnd;
    return DDERR_UNSUPPORTED;
}

static HRESULT KERNEL32_STUB clipper_GetClipList(void *this_ptr, void *lpClipList,
                                                 void *hWnd)
{
    (void)this_ptr;
    (void)lpClipList;
    (void)hWnd;
    return DDERR_UNSUPPORTED;
}

static HRESULT KERNEL32_STUB clipper_IsClipListChanged(void *this_ptr)
{
    (void)this_ptr;
    return DD_FALSE;
}

const IDirectDrawClipperVtbl clipper_vtbl = {
    .QueryInterface = clipper_QueryInterface,
    .AddRef = clipper_AddRef,
    .Release = clipper_Release,
    .SetHWnd = clipper_SetHWnd,
    .GetHWnd = clipper_GetHWnd,
    .SetClipList = clipper_SetClipList,
    .GetClipList = clipper_GetClipList,
    .IsClipListChanged = clipper_IsClipListChanged,
};
