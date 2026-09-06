/*
 * rb_surface_present.c
 *
 * SDL2 backend -- window-surface presentation and present-side diagnostics for
 * flip-chain surfaces.
 */

#include "rb_sdl2_priv.h"
#include "debug.h"

static rb_palette *rb_surface_present_get_palette(rb_palette_t pal)
{
    if (wine_handle_get_type((uint32_t)pal) != HANDLE_TYPE_RB_PALETTE)
        return NULL;
    return (rb_palette *)wine_handle_get((uint32_t)pal);
}

typedef struct {
    SDL_Window *window;
    SDL_Surface *surface;
    uint32_t flip_count;
} rb_sdl_window_surface_update_args;

static uintptr_t rb_sdl_update_window_surface_call(void *arg)
{
    rb_sdl_window_surface_update_args *a = arg;
    SDL_Surface *ws = SDL_GetWindowSurface(a->window);
    SDL_Surface *src = a->surface;
    SDL_Surface *converted = NULL;
    int ret;

    if (!ws)
        return 0;

    if (debug_level_at_least(1) && a->flip_count != 0 &&
        (a->flip_count & (a->flip_count - 1)) == 0) {
        const char *src_fmt = SDL_GetPixelFormatName(a->surface->format->format);
        const char *dst_fmt = SDL_GetPixelFormatName(ws->format->format);
        DEBUG("rb_surface: window flip=%u src_fmt=%s src_bpp=%u dst_fmt=%s dst_bpp=%u dst_palette=%d",
              a->flip_count, src_fmt, a->surface->format->BitsPerPixel,
              dst_fmt, ws->format->BitsPerPixel,
              ws->format->palette ? ws->format->palette->ncolors : 0);
    }

    if (a->surface->format->format != ws->format->format) {
        converted = SDL_ConvertSurface(a->surface, ws->format, 0);
        if (converted)
            src = converted;
    }

    if (src->w != ws->w || src->h != ws->h) {
        SDL_Rect dst = { 0, 0, ws->w, ws->h };
        ret = SDL_BlitScaled(src, NULL, ws, &dst);
    } else {
        ret = SDL_BlitSurface(src, NULL, ws, NULL);
    }

    if (debug_level_at_least(1) && a->flip_count != 0 &&
        (a->flip_count & (a->flip_count - 1)) == 0) {
        DEBUG("rb_surface: present ret=%d err=%s", ret, SDL_GetError());
    }

    if (converted)
        SDL_FreeSurface(converted);
    SDL_UpdateWindowSurface(a->window);
    return 1;
}

static void rb_debug_log_surface_sample(const char *tag, const rb_surface *surface,
                                        uint32_t count)
{
    const uint8_t *pixels;
    rb_palette *palette_state;
    SDL_Color c0 = { 0, 0, 0, 0 };
    SDL_Color c1 = { 0, 0, 0, 0 };
    SDL_Color c255 = { 0, 0, 0, 0 };
    int size;
    int nonzero = 0;
    int first_nonzero = -1;
    int palette_colors = 0;

    if (!debug_level_at_least(1) || !surface || !surface->surface ||
        !surface->surface->pixels)
        return;
    if ((count & (count - 1)) != 0)
        return;

    pixels = (const uint8_t *)surface->surface->pixels;
    size = surface->surface->pitch * surface->surface->h;
    for (int i = 0; i < size; i++) {
        if (pixels[i] != 0) {
            nonzero++;
            if (first_nonzero < 0)
                first_nonzero = i;
        }
    }

    palette_state = rb_surface_present_get_palette(surface->palette);
    if (palette_state && palette_state->palette) {
        palette_colors = palette_state->palette->ncolors;
        if (palette_colors > 0)
            c0 = palette_state->palette->colors[0];
        if (palette_colors > 1)
            c1 = palette_state->palette->colors[1];
        if (palette_colors > 255)
            c255 = palette_state->palette->colors[255];
    }

    DEBUG("rb_surface: %s flip=%u size=%dx%d pitch=%d nonzero=%d first=%d palette=%u colors=%d c0=%u,%u,%u c1=%u,%u,%u c255=%u,%u,%u dirty=%d",
          tag, count, surface->surface->w, surface->surface->h,
          surface->surface->pitch, nonzero, first_nonzero,
          (uint32_t)surface->palette, palette_colors,
          c0.r, c0.g, c0.b, c1.r, c1.g, c1.b, c255.r, c255.g, c255.b,
          surface->dirty);
}

int rb_surface_present_window(rb_window *wnd, rb_surface *primary,
                              rb_surface *backbuffer, uint32_t flip_count)
{
    if (!wnd || !wnd->window || !primary || !primary->surface)
        return RB_FAIL;

    if (backbuffer && backbuffer->surface) {
        uint8_t *tmp_pixels;
        uint8_t *tmp_buf;
        rb_palette_t tmp_palette;
        rb_sdl_window_surface_update_args args = {
            .window = wnd->window,
            .surface = backbuffer->surface,
            .flip_count = flip_count,
        };

        rb_debug_log_surface_sample("present-backbuffer", backbuffer, flip_count);
        rb_call_on_host_stack(rb_sdl_update_window_surface_call, &args);

        tmp_pixels = primary->surface->pixels;
        tmp_buf = primary->own_buf;
        tmp_palette = primary->palette;

        primary->surface->pixels = backbuffer->surface->pixels;
        backbuffer->surface->pixels = tmp_pixels;

        primary->own_buf = backbuffer->own_buf;
        backbuffer->own_buf = tmp_buf;

        primary->palette = backbuffer->palette;
        backbuffer->palette = tmp_palette;

        rb_surface_apply_palette(primary);
        rb_surface_apply_palette(backbuffer);
        backbuffer->dirty = 0;
        return RB_OK;
    }

    rb_debug_log_surface_sample("present-primary", primary, flip_count);
    rb_sdl_window_surface_update_args args = {
        .window = wnd->window,
        .surface = primary->surface,
        .flip_count = flip_count,
    };
    rb_call_on_host_stack(rb_sdl_update_window_surface_call, &args);
    return RB_OK;
}
