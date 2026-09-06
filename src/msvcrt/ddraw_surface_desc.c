#include <string.h>

#include "ddraw_priv.h"

extern int rb_surface_get_desc(rb_surface_t surf, int *w, int *h,
                               rb_pixel_format_t *format, int *pitch)
    __attribute__((weak));

typedef struct {
    uint32_t dwCaps;
} ddraw_guest_caps_t;

/*
 * Canonical host layout matching the guest DDSURFACEDESC2 shape used by the
 * current PE32 callers. Keep this translation isolated so the rest of the
 * DirectDraw path can work on normalized host data.
 */
typedef struct {
    uint32_t ddSize;
    uint32_t ddFlags;
    uint32_t dwHeight;
    uint32_t dwWidth;
    int32_t lPitch;
    uint32_t dwBackBufferCount;
    uint32_t dwMipMapCount;
    uint32_t dwAlphaBitDepth;
    uint32_t dwReserved;
    uint32_t lpSurface;
    uint8_t reserved[0x68 - 0x28];
    ddraw_guest_caps_t ddsCaps;
} __attribute__((packed)) ddraw_guest_desc_t;

/*
 * DirectDraw 1.x guest layout used by Doom95 and the sample coverage.
 * The caps field lives at offset 0x08 instead of the DDSURFACEDESC2 caps tail.
 */
typedef struct {
    uint32_t  ddSize;
    uint32_t  ddFlags;
    uint32_t  ddCaps;
    uint32_t  ddX;
    uint32_t  ddY;
    union {
        int32_t   lPitch;
        uint32_t  lWidth;
    };
    uint32_t  dwBackBufferCount;
    union {
        uint32_t  wWidth;
        uint32_t  wHeight;
        uint32_t  lWidth2;
        uint32_t  lHeight;
    };
    union {
        uint32_t  lpSurface;
        uint32_t  lpDDSurfaceDesc;
    };
} __attribute__((packed)) ddraw_guest_desc1_t;

typedef struct {
    uint32_t caps;
    int32_t pitch;
    uint32_t width;
    uint32_t height;
    uint32_t backbuffer_count;
    uint32_t lp_surface;
    uint32_t dd_flags;
} ddraw_surface_desc_data_t;

static void ddraw_normalize_surface_data(my_surface_t *surf, void *surface_ptr,
                                         ddraw_surface_desc_data_t *out)
{
    if (rb_surface_get_desc) {
        int width = 0;
        int height = 0;
        int pitch = 0;
        rb_pixel_format_t format;

        if (rb_surface_get_desc(surf->rb_surface, &width, &height, &format,
                                &pitch) == RB_OK) {
            surf->width = (uint32_t)width;
            surf->height = (uint32_t)height;
            surf->pitch = pitch;
        }
    }

    out->caps = surf->caps;
    out->pitch = surf->pitch;
    out->width = surf->width;
    out->height = surf->height;
    out->backbuffer_count = surf->next ? 1u : 0u;
    out->lp_surface = (uint32_t)(uintptr_t)surface_ptr;
    out->dd_flags = DDSD_CAPS | DDSD_WIDTH | DDSD_HEIGHT | DDSD_PITCH;
    if (surface_ptr)
        out->dd_flags |= DDSD_LPSURFACE;
}

static void ddraw_write_desc_v1(ddraw_guest_desc1_t *desc1,
                                const ddraw_surface_desc_data_t *data)
{
    memset(desc1, 0, sizeof(*desc1));
    desc1->ddSize = sizeof(*desc1);
    desc1->ddFlags = data->dd_flags;
    desc1->ddCaps = data->caps;
    desc1->lPitch = data->pitch;
    desc1->dwBackBufferCount = data->backbuffer_count;
    desc1->lWidth2 = data->width;
    desc1->lHeight = data->height;
    desc1->lpSurface = data->lp_surface;
}

static void ddraw_write_desc_v2(ddraw_guest_desc_t *desc,
                                const ddraw_surface_desc_data_t *data)
{
    uint32_t write_size = desc->ddSize;

    if (write_size < 0x20)
        write_size = 0x20;
    else if (write_size > sizeof(*desc))
        write_size = sizeof(*desc);

    memset(desc, 0, write_size);
    if (write_size >= 4)
        desc->ddSize = write_size;
    desc->ddFlags = data->dd_flags;
    if (write_size >= 0x68)
        desc->ddsCaps.dwCaps = data->caps;
    desc->lPitch = data->pitch;
    desc->dwBackBufferCount = data->backbuffer_count;
    desc->dwWidth = data->width;
    desc->dwHeight = data->height;
    desc->lpSurface = data->lp_surface;
}

void ddraw_fill_surface_desc(my_surface_t *surf, void *guest_desc,
                             void *surface_ptr)
{
    ddraw_guest_desc_t *desc = (ddraw_guest_desc_t *)guest_desc;
    ddraw_surface_desc_data_t data;

    if (!surf || !desc)
        return;

    ddraw_normalize_surface_data(surf, surface_ptr, &data);

    if (desc->ddSize == sizeof(ddraw_guest_desc1_t)) {
        ddraw_write_desc_v1((ddraw_guest_desc1_t *)desc, &data);
        return;
    }

    ddraw_write_desc_v2(desc, &data);
}

void ddraw_init_display_mode_desc(void *guest_desc, uint32_t width,
                                  uint32_t height, uint32_t caps)
{
    ddraw_guest_desc_t *desc = (ddraw_guest_desc_t *)guest_desc;

    if (!desc)
        return;

    memset(desc, 0, sizeof(*desc));
    desc->ddSize = sizeof(*desc);
    desc->ddFlags = DDSD_WIDTH | DDSD_HEIGHT;
    desc->ddsCaps.dwCaps = caps;
    desc->dwWidth = width;
    desc->dwHeight = height;
}

void ddraw_parse_surface_desc(const void *guest_desc, uint32_t *caps,
                              uint32_t *width, uint32_t *height,
                              uint32_t *backbuffers)
{
    const ddraw_guest_desc_t *desc = (const ddraw_guest_desc_t *)guest_desc;

    if (desc->ddSize == sizeof(ddraw_guest_desc1_t)) {
        const ddraw_guest_desc1_t *desc1 =
            (const ddraw_guest_desc1_t *)guest_desc;

        *caps = desc1->ddCaps;
        *width = desc1->lWidth;
        *height = desc1->lHeight;
        *backbuffers = desc1->dwBackBufferCount;
        return;
    }

    *caps = desc->ddsCaps.dwCaps;
    *width = desc->dwWidth;
    *height = desc->dwHeight;
    *backbuffers = desc->dwBackBufferCount;
}
