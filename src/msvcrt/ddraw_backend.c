/*
 * ddraw_backend.c
 *
 * Internal DirectDraw backend helper support:
 * - one-time backend initialization
 * - backend pixel-format mapping
 * - primary/backbuffer surface allocation helpers used by CreateSurface
 */

#include "ddraw_priv.h"
#include "include/handle_manager.h"

extern int rb_init(void) __attribute__((weak));
extern rb_surface_t rb_surface_create(int w, int h, rb_pixel_format_t format,
                                      rb_palette_t palette,
                                      uint32_t flags) __attribute__((weak));
extern rb_surface_t rb_surface_create_flip_chain(rb_window_t win, int w, int h,
                                                 rb_pixel_format_t format,
                                                 rb_palette_t palette,
                                                 int backbuffer_count) __attribute__((weak));
extern int rb_surface_destroy(rb_surface_t surf) __attribute__((weak));

typedef struct {
    void *window;
    uint32_t sdl_window_id;
    uintptr_t native_window_id;
    uintptr_t guest_hwnd;
    int is_visible;
    int is_minimized;
    int is_maximized;
    rb_surface_t primary_surface;
    rb_surface_t backbuffer;
} ddraw_backend_window_state;

static int g_ddraw_backend_inited = 0;
static int g_ddraw_backend_available = 0;

static rb_pixel_format_t ddraw_bpp_to_format(uint32_t bpp)
{
    switch (bpp) {
    case 8:
        return RB_FORMAT_8BIT;
    case 15:
        return RB_FORMAT_15BIT;
    case 16:
        return RB_FORMAT_16BIT;
    case 32:
        return RB_FORMAT_32BIT;
    default:
        return RB_FORMAT_8BIT;
    }
}

static ddraw_backend_window_state *ddraw_get_backend_window(rb_window_t win)
{
    if (!win || wine_handle_get_type((uint32_t)win) != HANDLE_TYPE_RB_WINDOW)
        return NULL;
    return (ddraw_backend_window_state *)wine_handle_get((uint32_t)win);
}

int ddraw_ensure_backend(void)
{
    if (!g_ddraw_backend_inited) {
        g_ddraw_backend_inited = 1;
        g_ddraw_backend_available = (rb_init && rb_init() == 0) ? 1 : 0;
    }
    return g_ddraw_backend_available;
}

HRESULT ddraw_create_flip_chain_surface(my_dd_t *dd, uint32_t width,
                                        uint32_t height, uint32_t backbuffers,
                                        uint32_t caps,
                                        my_surface_t **primary_out,
                                        my_surface_t **backbuffer_out,
                                        void **lplpDDSurface)
{
    rb_surface_t rb_primary = 0;
    rb_surface_t rb_backbuffer = 0;
    my_surface_t *primary = NULL;
    my_surface_t *backbuffer = NULL;
    ddraw_backend_window_state *wnd;

    if (!rb_surface_create_flip_chain)
        return DDERR_UNSUPPORTED;

    rb_primary = rb_surface_create_flip_chain(dd->rb_window, (int)width,
                                              (int)height,
                                              ddraw_bpp_to_format(dd->current_mode_bpp),
                                              0, (int)backbuffers);
    if (!rb_primary)
        return DDERR_OUTOFMEMORY;

    primary = ddraw_surface_alloc(dd, rb_primary, caps);
    if (!primary)
        return DDERR_OUTOFMEMORY;

    wnd = ddraw_get_backend_window(dd->rb_window);
    rb_backbuffer = wnd ? wnd->backbuffer : 0;
    DEBUG("ddraw: flip chain primary=%u wnd=%p backend_backbuffer=%u",
          (unsigned int)rb_primary, (void *)wnd, (unsigned int)rb_backbuffer);

    if (!rb_backbuffer && wnd && rb_surface_create) {
        rb_backbuffer = rb_surface_create((int)width, (int)height,
                                          ddraw_bpp_to_format(dd->current_mode_bpp),
                                          0, RB_SURFACE_BACK);
        if (rb_backbuffer)
            wnd->backbuffer = rb_backbuffer;
        DEBUG("ddraw: created fallback backbuffer=%u",
              (unsigned int)rb_backbuffer);
    }

    if (rb_backbuffer) {
        backbuffer = ddraw_surface_alloc(dd, rb_backbuffer,
                                         DDSCAPS_BACKBUFFER | DDSCAPS_FLIP);
        if (!backbuffer) {
            ddraw_free(primary);
            if (rb_primary && rb_surface_destroy)
                rb_surface_destroy(rb_primary);
            return DDERR_OUTOFMEMORY;
        }
        primary->next = backbuffer;
    }

    dd->primary_surface = primary;
    primary->ref_count++;

    *primary_out = primary;
    *backbuffer_out = backbuffer;
    *lplpDDSurface = FORCE_PTR_RETURN(primary);
    return DD_OK;
}

HRESULT ddraw_create_regular_surface(my_dd_t *dd, uint32_t width,
                                     uint32_t height, uint32_t caps,
                                     my_surface_t **primary_out,
                                     void **lplpDDSurface)
{
    uint32_t rb_flags = 0;
    rb_surface_t rb_primary = 0;
    my_surface_t *primary;

    if (caps & DDSCAPS_PRIMARYSURFACE)
        rb_flags |= RB_SURFACE_PRIMARY;
    if (caps & DDSCAPS_OFFSCREENPLAIN)
        rb_flags |= RB_SURFACE_OFFSCREEN;

    if (!rb_surface_create)
        return DDERR_UNSUPPORTED;

    rb_primary = rb_surface_create((int)width, (int)height,
                                   ddraw_bpp_to_format(dd->current_mode_bpp),
                                   0, rb_flags);
    if (!rb_primary)
        return DDERR_OUTOFMEMORY;

    primary = ddraw_surface_alloc(dd, rb_primary, caps);
    if (!primary) {
        if (rb_primary && rb_surface_destroy)
            rb_surface_destroy(rb_primary);
        return DDERR_OUTOFMEMORY;
    }

    if (caps & DDSCAPS_PRIMARYSURFACE) {
        dd->primary_surface = primary;
        primary->ref_count++;
    }

    *primary_out = primary;
    *lplpDDSurface = FORCE_PTR_RETURN(primary);
    return DD_OK;
}
