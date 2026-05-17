#include "rb_sdl2_priv.h"
#include <stdlib.h>
#include <string.h>

static inline rb_surface *get_surface(rb_surface_t surf)
{
    if (wine_handle_get_type((uint32_t)surf) != HANDLE_TYPE_RB_SURFACE)
        return NULL;
    return (rb_surface *)wine_handle_get((uint32_t)surf);
}

static inline rb_window *get_window(rb_window_t win)
{
    if (wine_handle_get_type((uint32_t)win) != HANDLE_TYPE_RB_WINDOW)
        return NULL;
    return (rb_window *)wine_handle_get((uint32_t)win);
}

static inline rb_palette *get_palette(rb_palette_t pal)
{
    if (wine_handle_get_type((uint32_t)pal) != HANDLE_TYPE_RB_PALETTE)
        return NULL;
    return (rb_palette *)wine_handle_get((uint32_t)pal);
}

static int rb_format_to_bpp(rb_pixel_format_t format)
{
    switch (format) {
    case RB_FORMAT_8BIT:
        return 8;
    case RB_FORMAT_15BIT:
        return 15;
    case RB_FORMAT_16BIT:
        return 16;
    case RB_FORMAT_32BIT:
        return 32;
    default:
        return 0;
    }
}

static int rb_surface_pitch_for_bpp(int width, int bpp)
{
    if (bpp == 8)
        return width;
    if (bpp <= 16)
        return width * 2;
    return width * 4;
}

typedef struct {
    uint8_t *buf;
    int w;
    int h;
    int bpp;
    int sdl_bpp;
    int pitch;
    uint32_t rmask;
    uint32_t gmask;
    uint32_t bmask;
    uint32_t amask;
} rb_sdl_surface_create_args;

static uintptr_t rb_sdl_pixel_format_masks_call(void *arg)
{
    rb_sdl_surface_create_args *a = arg;
    a->sdl_bpp = a->bpp;
    return (uintptr_t)SDL_PixelFormatEnumToMasks(SDL_PIXELFORMAT_XRGB1555,
                                                 &a->sdl_bpp,
                                                 &a->rmask, &a->gmask,
                                                 &a->bmask, &a->amask);
}

static uintptr_t rb_sdl_create_surface_from_call(void *arg)
{
    rb_sdl_surface_create_args *a = arg;
    return (uintptr_t)SDL_CreateRGBSurfaceFrom(a->buf, a->w, a->h, a->bpp, a->pitch,
                                               a->rmask, a->gmask, a->bmask, a->amask);
}

typedef struct {
    SDL_Surface *surface;
    SDL_Palette *palette;
} rb_sdl_surface_palette_args;

static uintptr_t rb_sdl_set_surface_palette_call(void *arg)
{
    rb_sdl_surface_palette_args *a = arg;
    return (uintptr_t)SDL_SetSurfacePalette(a->surface, a->palette);
}

static void rb_surface_apply_palette(rb_surface *surface_state)
{
    rb_palette *palette_state;
    rb_sdl_surface_palette_args args;

    if (!surface_state || !surface_state->surface || !surface_state->palette)
        return;

    palette_state = get_palette(surface_state->palette);
    if (!palette_state || !palette_state->palette)
        return;

    args.surface = surface_state->surface;
    args.palette = palette_state->palette;
    rb_call_on_host_stack(rb_sdl_set_surface_palette_call, &args);
}

static uintptr_t rb_sdl_free_surface_call(void *arg)
{
    SDL_FreeSurface((SDL_Surface *)arg);
    return 0;
}

typedef struct {
    SDL_Surface *dst;
    SDL_Rect rect;
    uint32_t color;
} rb_sdl_fill_rect_args;

static uintptr_t rb_sdl_fill_rect_call(void *arg)
{
    rb_sdl_fill_rect_args *a = arg;
    return (uintptr_t)SDL_FillRect(a->dst, &a->rect, a->color);
}

typedef struct {
    SDL_Surface *src;
    SDL_Surface *dst;
    SDL_Rect src_rect;
    SDL_Rect dst_rect;
} rb_sdl_blt_args;

static uintptr_t rb_sdl_soft_stretch_call(void *arg)
{
    rb_sdl_blt_args *a = arg;
    return (uintptr_t)SDL_SoftStretch(a->src, &a->src_rect, a->dst, &a->dst_rect);
}

static uintptr_t rb_sdl_blt_surface_call(void *arg)
{
    rb_sdl_blt_args *a = arg;
    return (uintptr_t)SDL_BlitSurface(a->src, &a->src_rect, a->dst, &a->dst_rect);
}

typedef struct {
    SDL_Window *window;
    SDL_Surface *surface;
} rb_sdl_window_surface_update_args;

static uintptr_t rb_sdl_update_window_surface_call(void *arg)
{
    rb_sdl_window_surface_update_args *a = arg;
    SDL_Surface *ws = SDL_GetWindowSurface(a->window);
    if (!ws)
        return 0;

    SDL_BlitSurface(a->surface, NULL, ws, NULL);
    SDL_UpdateWindowSurface(a->window);
    return 1;
}

rb_surface_t rb_surface_create(int w, int h, rb_pixel_format_t format,
                               rb_palette_t palette, uint32_t flags)
{
    if (w <= 0 || h <= 0)
        return 0;

    int bpp = rb_format_to_bpp(format);
    if (bpp == 0)
        return 0;

    int pitch = rb_surface_pitch_for_bpp(w, bpp);
    int buf_size = pitch * h;
    uint8_t *buf = rb_host_malloc(buf_size);
    if (!buf)
        return 0;
    memset(buf, 0, buf_size);

    uint32_t rmask = 0, gmask = 0, bmask = 0, amask = 0;
    if (bpp == 15) {
        /* SDL2 requires SDL_PIXELFORMAT_XRGB1555 masks; hardcoding 0x7C00/0x03E0/0x001C fails.
         * Use SDL_PixelFormatEnumToMasks to get the correct masks for this SDL2 build. */
        rb_sdl_surface_create_args mask_args = { .bpp = 0 };
        int mask_ret = (int)rb_call_on_host_stack(rb_sdl_pixel_format_masks_call, &mask_args);
        if (!mask_ret) {
            rb_host_free(buf);
            return 0;
        }
        rmask = mask_args.rmask;
        gmask = mask_args.gmask;
        bmask = mask_args.bmask;
        amask = mask_args.amask;
        bpp = mask_args.sdl_bpp;  /* may be 15 or 16 depending on SDL2 version */
    } else if (bpp == 16) { rmask = 0xF800; gmask = 0x07E0; bmask = 0x001F; }
    else if (bpp == 32) { rmask = 0xFF000000; gmask = 0x00FF0000; bmask = 0x0000FF00; amask = 0x000000FF; }

    rb_sdl_surface_create_args create_args = {
        .buf = buf, .w = w, .h = h, .bpp = bpp, .pitch = pitch,
        .rmask = rmask, .gmask = gmask, .bmask = bmask, .amask = amask,
    };
    SDL_Surface *surface = (SDL_Surface *)rb_call_on_host_stack(rb_sdl_create_surface_from_call, &create_args);
    if (!surface) {
        rb_host_free(buf);
        return 0;
    }

    if (palette) {
        rb_palette *p = get_palette(palette);
        if (p && p->palette) {
            rb_sdl_surface_palette_args args = { surface, p->palette };
            rb_call_on_host_stack(rb_sdl_set_surface_palette_call, &args);
        }
    }

    rb_surface *s = rb_host_malloc(sizeof(*s));
    if (!s) {
        rb_call_on_host_stack(rb_sdl_free_surface_call, surface);
        rb_host_free(buf);
        return 0;
    }
    s->surface = surface;
    s->own_buf = buf;
    s->palette = palette;
    s->dirty = 0;
    s->window = 0;
    s->pitch = pitch;
    s->flags = flags;

    return (rb_surface_t)wine_handle_alloc(HANDLE_TYPE_RB_SURFACE, s);
}

rb_surface_t rb_surface_create_flip_chain(rb_window_t win,
                                          int w, int h,
                                          rb_pixel_format_t format,
                                          rb_palette_t palette,
                                          int backbuffer_count)
{
    rb_surface_t primary = rb_surface_create(w, h, format, palette,
                                             RB_SURFACE_PRIMARY | RB_SURFACE_FLIP);
    if (!primary) return 0;

    rb_surface *ps = get_surface(primary);
    if (ps) {
        ps->window = win;
        ps->flags |= RB_SURFACE_PRIMARY | RB_SURFACE_FLIP;
        ps->flags &= ~RB_SURFACE_BACK;
    }

    rb_window *wnd = get_window(win);
    if (wnd) {
        if (wnd->primary_surface && wnd->primary_surface != primary) {
            rb_surface *old_primary = get_surface(wnd->primary_surface);
            if (old_primary) {
                old_primary->window = 0;
                old_primary->flags &= ~(RB_SURFACE_PRIMARY | RB_SURFACE_FLIP | RB_SURFACE_BACK);
            }
            wnd->primary_surface = 0;
        }

        /* Clean up any existing backbuffer before creating a new one. */
        if (wnd->backbuffer) {
            rb_surface *old_backbuffer = get_surface(wnd->backbuffer);
            if (old_backbuffer) {
                old_backbuffer->window = 0;
                old_backbuffer->flags &= ~(RB_SURFACE_PRIMARY | RB_SURFACE_FLIP | RB_SURFACE_BACK);
            }
            rb_surface_destroy(wnd->backbuffer);
            wnd->backbuffer = 0;
        }
        wnd->primary_surface = primary;

        if (backbuffer_count > 0) {
            rb_surface_t backbuf = rb_surface_create(w, h, format, palette,
                                                      RB_SURFACE_BACK);
            if (backbuf) {
                rb_surface *bs = get_surface(backbuf);
                if (bs) {
                    bs->window = win;
                    bs->flags |= RB_SURFACE_BACK;
                    bs->flags &= ~(RB_SURFACE_PRIMARY | RB_SURFACE_FLIP);
                }
                wnd->backbuffer = backbuf;  /* window owns the backbuffer handle */
            }
        }
    }

    return primary;
}

int rb_surface_destroy(rb_surface_t surf)
{
    rb_surface *s = get_surface(surf);
    rb_window *wnd = NULL;
    if (!s) return RB_FAIL;

    if (s->window)
        wnd = get_window(s->window);

    if (wnd) {
        if (wnd->primary_surface == surf) {
            rb_surface_t backbuffer = wnd->backbuffer;
            wnd->primary_surface = 0;
            wnd->backbuffer = 0;
            if (backbuffer && backbuffer != surf) {
                rb_surface *bs = get_surface(backbuffer);
                if (bs) {
                    bs->window = 0;
                    bs->flags &= ~(RB_SURFACE_PRIMARY | RB_SURFACE_FLIP | RB_SURFACE_BACK);
                }
                rb_surface_destroy(backbuffer);
            }
        } else if (wnd->backbuffer == surf) {
            wnd->backbuffer = 0;
        }
    }

    rb_call_on_host_stack(rb_sdl_free_surface_call, s->surface);
    rb_host_free(s->own_buf);
    rb_host_free(s);
    wine_handle_free((uint32_t)surf);
    return RB_OK;
}

int rb_surface_lock(rb_surface_t surf, const rb_rect_t *rect,
                    uint8_t **out_data, int *out_pitch)
{
    rb_surface *s = get_surface(surf);
    if (!s || !s->surface || !out_data || !out_pitch)
        return RB_FAIL;
    if (rect && (rect->x < 0 || rect->y < 0 ||
                 rect->x > s->surface->w || rect->y > s->surface->h))
        return RB_FAIL;

    *out_data = s->surface->pixels;
    if (rect && (rect->x != 0 || rect->y != 0)) {
        *out_data += rect->y * s->surface->pitch
                   + rect->x * (s->surface->format->BytesPerPixel);
    }
    *out_pitch = s->surface->pitch;
    return RB_OK;
}

int rb_surface_unlock(rb_surface_t surf)
{
    rb_surface *s = get_surface(surf);
    if (!s) return RB_FAIL;
    s->dirty = 1;
    return RB_OK;
}

int rb_surface_blt(rb_surface_t dst, const rb_rect_t *dst_rect,
                   rb_surface_t src, const rb_rect_t *src_rect,
                   uint32_t color, uint32_t flags)
{
    rb_surface *ds = get_surface(dst);
    if (!ds || !ds->surface) return RB_FAIL;

    if (flags & RB_BLT_COLORFILL) {
        SDL_Rect dr;
        if (dst_rect) {
            dr = (SDL_Rect){dst_rect->x, dst_rect->y, dst_rect->w, dst_rect->h};
        } else {
            dr = (SDL_Rect){0, 0, ds->surface->w, ds->surface->h};
        }
        rb_sdl_fill_rect_args args = { ds->surface, dr, color };
        int ret = (int)rb_call_on_host_stack(rb_sdl_fill_rect_call, &args);
        if (ret < 0)
            return RB_FAIL;
        ds->dirty = 1;
        return RB_OK;
    }

    if (flags & RB_BLT_SRCCOPY) {
        rb_surface *ss = get_surface(src);
        if (!ss || !ss->surface) return RB_FAIL;

        SDL_Rect sr, dr;
        if (src_rect) {
            sr = (SDL_Rect){src_rect->x, src_rect->y, src_rect->w, src_rect->h};
        } else {
            sr = (SDL_Rect){0, 0, ss->surface->w, ss->surface->h};
        }
        if (dst_rect) {
            dr = (SDL_Rect){dst_rect->x, dst_rect->y, dst_rect->w, dst_rect->h};
        } else {
            dr = (SDL_Rect){0, 0, ds->surface->w, ds->surface->h};
        }

        rb_sdl_blt_args args = { ss->surface, ds->surface, sr, dr };
        if (sr.w != dr.w || sr.h != dr.h) {
            int ret = (int)rb_call_on_host_stack(rb_sdl_soft_stretch_call, &args);
            if (ret < 0)
                return RB_FAIL;
        } else {
            int ret = (int)rb_call_on_host_stack(rb_sdl_blt_surface_call, &args);
            if (ret < 0)
                return RB_FAIL;
        }
        ds->dirty = 1;
        return RB_OK;
    }

    return RB_FAIL;
}

int rb_surface_flip(rb_surface_t surf)
{
    rb_surface *s = get_surface(surf);
    rb_window *wnd;
    rb_surface *primary;
    rb_surface *backbuffer;
    if (!s || !s->surface) return RB_FAIL;

    if (!s->window) {
        s->dirty = 0;
        return RB_OK;
    }

    wnd = get_window(s->window);
    if (!wnd || !wnd->window) {
        s->dirty = 0;
        return RB_OK;
    }

    primary = get_surface(wnd->primary_surface);
    if (!primary || wnd->primary_surface != surf)
        return RB_FAIL;

    backbuffer = get_surface(wnd->backbuffer);
    if (backbuffer && backbuffer->surface) {
        rb_sdl_window_surface_update_args args = { wnd->window, backbuffer->surface };
        rb_call_on_host_stack(rb_sdl_update_window_surface_call, &args);

        {
            uint8_t *tmp_pixels = primary->surface->pixels;
            uint8_t *tmp_buf = primary->own_buf;
            rb_palette_t tmp_palette = primary->palette;

            primary->surface->pixels = backbuffer->surface->pixels;
            backbuffer->surface->pixels = tmp_pixels;

            primary->own_buf = backbuffer->own_buf;
            backbuffer->own_buf = tmp_buf;

            primary->palette = backbuffer->palette;
            backbuffer->palette = tmp_palette;
        }

        rb_surface_apply_palette(primary);
        rb_surface_apply_palette(backbuffer);
        backbuffer->dirty = 0;
    } else {
        rb_sdl_window_surface_update_args args = { wnd->window, primary->surface };
        rb_call_on_host_stack(rb_sdl_update_window_surface_call, &args);
    }

    primary->dirty = 0;
    return RB_OK;
}

int rb_surface_get_desc(rb_surface_t surf,
                        int *w, int *h, rb_pixel_format_t *format,
                        int *pitch)
{
    rb_surface *s = get_surface(surf);
    if (!s || !s->surface) return RB_FAIL;

    if (w) *w = s->surface->w;
    if (h) *h = s->surface->h;
    if (format) {
        int bpp = s->surface->format->BitsPerPixel;
        switch (bpp) {
            case 8:  *format = RB_FORMAT_8BIT;  break;
            case 15: *format = RB_FORMAT_15BIT; break;
            case 16: *format = RB_FORMAT_16BIT; break;
            case 32: *format = RB_FORMAT_32BIT; break;
            default: *format = RB_FORMAT_8BIT;  break;
        }
    }
    if (pitch) *pitch = s->surface->pitch;
    return RB_OK;
}

int rb_surface_set_palette(rb_surface_t surf, rb_palette_t pal)
{
    rb_surface *s = get_surface(surf);
    if (!s || !s->surface) return RB_FAIL;

    rb_palette *p = get_palette(pal);
    if (!p || !p->palette) return RB_FAIL;

    rb_sdl_surface_palette_args args = { s->surface, p->palette };
    int ret = (int)rb_call_on_host_stack(rb_sdl_set_surface_palette_call, &args);
    if (ret < 0)
        return RB_FAIL;
    s->palette = pal;
    return RB_OK;
}
