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

rb_surface_t rb_surface_create(int w, int h, rb_pixel_format_t format,
                               rb_palette_t palette, uint32_t flags)
{
    (void)flags;

    if (w <= 0 || h <= 0)
        return 0;

    int bpp = rb_format_to_bpp(format);
    if (bpp == 0)
        return 0;

    int pitch = rb_surface_pitch_for_bpp(w, bpp);
    int buf_size = pitch * h;
    uint8_t *buf = malloc(buf_size);
    if (!buf)
        return 0;
    memset(buf, 0, buf_size);

    uint32_t rmask = 0, gmask = 0, bmask = 0, amask = 0;
    if (bpp == 15) {
        /* SDL2 requires SDL_PIXELFORMAT_XRGB1555 masks; hardcoding 0x7C00/0x03E0/0x001C fails.
         * Use SDL_PixelFormatEnumToMasks to get the correct masks for this SDL2 build. */
        int sdl_bpp = 0;
        if (!SDL_PixelFormatEnumToMasks(SDL_PIXELFORMAT_XRGB1555, &sdl_bpp, &rmask, &gmask, &bmask, &amask)) {
            free(buf);
            return 0;
        }
        bpp = sdl_bpp;  /* may be 15 or 16 depending on SDL2 version */
    } else if (bpp == 16) { rmask = 0xF800; gmask = 0x07E0; bmask = 0x001F; }
    else if (bpp == 32) { rmask = 0xFF000000; gmask = 0x00FF0000; bmask = 0x0000FF00; amask = 0x000000FF; }

    uintptr_t saved_gs = rb_host_context_enter();
    SDL_Surface *surface = SDL_CreateRGBSurfaceFrom(buf, w, h, bpp, pitch,
                                                    rmask, gmask, bmask, amask);
    rb_host_context_leave(saved_gs);
    if (!surface) {
        free(buf);
        return 0;
    }

    if (palette) {
        rb_palette *p = get_palette(palette);
        if (p && p->palette) {
            saved_gs = rb_host_context_enter();
            SDL_SetSurfacePalette(surface, p->palette);
            rb_host_context_leave(saved_gs);
        }
    }

    rb_surface *s = malloc(sizeof(*s));
    if (!s) {
        saved_gs = rb_host_context_enter();
        SDL_FreeSurface(surface);
        rb_host_context_leave(saved_gs);
        free(buf);
        return 0;
    }
    s->surface = surface;
    s->own_buf = buf;
    s->palette = palette;
    s->dirty = 0;
    s->window = 0;
    s->pitch = pitch;

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
    if (ps) ps->window = win;

    rb_window *wnd = get_window(win);
    if (wnd) {
        /* Clean up any existing backbuffer before creating a new one */
        if (wnd->backbuffer) {
            rb_surface_destroy(wnd->backbuffer);
            wnd->backbuffer = 0;
        }
        wnd->primary_surface = primary;

        if (backbuffer_count > 0) {
            rb_surface_t backbuf = rb_surface_create(w, h, format, palette,
                                                      RB_SURFACE_BACK);
            if (backbuf) {
                rb_surface *bs = get_surface(backbuf);
                if (bs) bs->window = win;
                wnd->backbuffer = backbuf;  /* window owns the backbuffer handle */
            }
        }
    }

    return primary;
}

int rb_surface_destroy(rb_surface_t surf)
{
    rb_surface *s = get_surface(surf);
    if (!s) return RB_FAIL;

    uintptr_t saved_gs = rb_host_context_enter();
    SDL_FreeSurface(s->surface);
    rb_host_context_leave(saved_gs);
    if (s->own_buf) free(s->own_buf);
    free(s);
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
        uintptr_t saved_gs = rb_host_context_enter();
        int ret = SDL_FillRect(ds->surface, &dr, color);
        rb_host_context_leave(saved_gs);
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

        if (sr.w != dr.w || sr.h != dr.h) {
            uintptr_t saved_gs = rb_host_context_enter();
            int ret = SDL_SoftStretch(ss->surface, &sr, ds->surface, &dr);
            rb_host_context_leave(saved_gs);
            if (ret < 0)
                return RB_FAIL;
        } else {
            uintptr_t saved_gs = rb_host_context_enter();
            int ret = SDL_BlitSurface(ss->surface, &sr, ds->surface, &dr);
            rb_host_context_leave(saved_gs);
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
    if (!s || !s->surface) return RB_FAIL;

    if (s->window) {
        rb_window *wnd = get_window(s->window);
        if (wnd && wnd->window) {
            uintptr_t saved_gs = rb_host_context_enter();
            SDL_Surface *ws = SDL_GetWindowSurface(wnd->window);
            if (ws) {
                SDL_BlitSurface(s->surface, NULL, ws, NULL);
                SDL_UpdateWindowSurface(wnd->window);
            }
            rb_host_context_leave(saved_gs);
        }

        /* Swap with backbuffer: primary becomes backbuffer for next frame */
        if (wnd->backbuffer) {
            rb_surface *bs = get_surface(wnd->backbuffer);
            if (bs && bs->surface && bs->surface->pixels) {
                uint8_t *tmp = s->surface->pixels;
                s->surface->pixels = bs->surface->pixels;
                bs->surface->pixels = tmp;
                /* Also swap own_buf pointers */
                uint8_t *tmp_buf = s->own_buf;
                s->own_buf = bs->own_buf;
                bs->own_buf = tmp_buf;
            }
        }
    }
    s->dirty = 0;
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

    uintptr_t saved_gs = rb_host_context_enter();
    int ret = SDL_SetSurfacePalette(s->surface, p->palette);
    rb_host_context_leave(saved_gs);
    if (ret < 0)
        return RB_FAIL;
    s->palette = pal;
    return RB_OK;
}
