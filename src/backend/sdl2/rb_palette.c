/*
 * rb_palette.c
 *
 * SDL2 backend — palette functions.
 * Implements palette creation, destruction, and color get/set.
 */

#include "rb_sdl2_priv.h"
#include <stdlib.h>

static inline rb_palette *get_palette(rb_palette_t pal)
{
    if (wine_handle_get_type((uint32_t)pal) != HANDLE_TYPE_RB_PALETTE)
        return NULL;
    return (rb_palette *)wine_handle_get((uint32_t)pal);
}

typedef struct {
    SDL_Palette *palette;
    const SDL_Color *colors;
    int start;
    int count;
} rb_sdl_set_palette_colors_args;

static uintptr_t rb_sdl_alloc_palette_call(void *arg)
{
    return (uintptr_t)SDL_AllocPalette(*(int *)arg);
}

static uintptr_t rb_sdl_set_palette_colors_call(void *arg)
{
    rb_sdl_set_palette_colors_args *a = arg;
    return (uintptr_t)SDL_SetPaletteColors(a->palette, a->colors, a->start, a->count);
}

static uintptr_t rb_sdl_free_palette_call(void *arg)
{
    SDL_FreePalette((SDL_Palette *)arg);
    return 0;
}

rb_palette_t rb_palette_create(int num_colors)
{
    if (num_colors <= 0)
        return 0;

    SDL_Palette *pal = (SDL_Palette *)rb_call_on_host_stack(rb_sdl_alloc_palette_call, &num_colors);
    if (!pal)
        return 0;

    rb_palette *p = rb_host_malloc(sizeof(*p));
    if (!p) {
        rb_call_on_host_stack(rb_sdl_free_palette_call, pal);
        return 0;
    }
    p->palette = pal;
    p->num_colors = num_colors;

    return (rb_palette_t)wine_handle_alloc(HANDLE_TYPE_RB_PALETTE, p);
}

int rb_palette_destroy(rb_palette_t pal)
{
    rb_palette *p = get_palette(pal);
    if (!p)
        return RB_FAIL;

    rb_call_on_host_stack(rb_sdl_free_palette_call, p->palette);
    rb_host_free(p);
    wine_handle_free((uint32_t)pal);
    return RB_OK;
}

int rb_palette_set_colors(rb_palette_t pal,
                          uint32_t start, uint32_t count,
                          const uint32_t *colors)
{
    rb_palette *p = get_palette(pal);
    if (!p || !colors)
        return RB_FAIL;

    if (start >= (uint32_t)p->num_colors)
        return RB_FAIL;
    if (start + count > (uint32_t)p->num_colors)
        count = (uint32_t)p->num_colors - start;

    if (count == 0)
        return RB_OK;

    SDL_Color *arr = rb_host_malloc(sizeof(SDL_Color) * count);
    if (!arr)
        return RB_FAIL;

    for (uint32_t i = 0; i < count; i++) {
        uint32_t c = colors[i];  /* format: 0x00BBGGRR */
        arr[i].r = c & 0xFF;
        arr[i].g = (c >> 8) & 0xFF;
        arr[i].b = (c >> 16) & 0xFF;
        arr[i].a = SDL_ALPHA_OPAQUE;
    }

    rb_sdl_set_palette_colors_args args = { p->palette, arr, (int)start, (int)count };
    int ret = (int)rb_call_on_host_stack(rb_sdl_set_palette_colors_call, &args);
    rb_host_free(arr);
    return ret == 0 ? RB_OK : RB_FAIL;
}

int rb_palette_get_colors(rb_palette_t pal,
                          uint32_t start, uint32_t count,
                          uint32_t *colors)
{
    rb_palette *p = get_palette(pal);
    if (!p || !colors)
        return RB_FAIL;

    if (start >= (uint32_t)p->num_colors)
        return RB_FAIL;
    if (start + count > (uint32_t)p->num_colors)
        count = (uint32_t)p->num_colors - start;

    if (count == 0)
        return RB_OK;

    /* Read directly from palette->colors (SDL_GetPaletteColors not available in all SDL2 versions) */
    for (uint32_t i = 0; i < count; i++) {
        SDL_Color *c = &p->palette->colors[start + i];
        /* Convert to 0x00BBGGRR */
        colors[i] = ((uint32_t)c->b << 16) |
                    ((uint32_t)c->g << 8) |
                    (uint32_t)c->r;
    }
    return RB_OK;
}
