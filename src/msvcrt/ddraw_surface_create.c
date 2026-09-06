/*
 * ddraw_surface_create.c
 *
 * Internal DirectDraw surface-create orchestration:
 * - guest descriptor parsing and mode-size fallback
 * - primary-surface cooperative-level validation
 * - flip-chain vs regular-surface metadata setup
 */

#include "ddraw_priv.h"

static void ddraw_init_surface_geometry(my_surface_t *surf, uint32_t width,
                                        uint32_t height)
{
    if (!surf)
        return;

    surf->width = width;
    surf->height = height;
    surf->pitch = (int32_t)width;
}

HRESULT ddraw_create_surface_from_desc(my_dd_t *dd, void *ddsd,
                                       void **lplpDDSurface)
{
    uint32_t caps = 0;
    uint32_t width = 0;
    uint32_t height = 0;
    uint32_t backbuffers = 0;

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
                                             caps, &primary, &backbuffer,
                                             lplpDDSurface);
        if (hr != DD_OK)
            return hr;

        ddraw_init_surface_geometry(primary, width, height);
        ddraw_init_surface_geometry(backbuffer, width, height);
        return DD_OK;
    }

    {
        my_surface_t *primary = NULL;
        HRESULT hr;

        hr = ddraw_create_regular_surface(dd, width, height, caps,
                                          &primary, lplpDDSurface);
        if (hr != DD_OK)
            return hr;

        ddraw_init_surface_geometry(primary, width, height);
    }

    return DD_OK;
}
