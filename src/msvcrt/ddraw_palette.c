/*
 * ddraw_palette.c
 *
 * Internal DirectDraw palette object support:
 * - IDirectDraw::CreatePalette backend allocation/initialization
 * - IDirectDrawPalette COM lifetime and entry access
 */

#include <string.h>

#include "ddraw_priv.h"

extern rb_palette_t rb_palette_create(int num_colors) __attribute__((weak));
extern int rb_palette_destroy(rb_palette_t pal) __attribute__((weak));
extern int rb_palette_set_colors(rb_palette_t pal, uint32_t start, uint32_t count,
                                 const uint32_t *colors) __attribute__((weak));
extern int rb_palette_get_colors(rb_palette_t pal, uint32_t start, uint32_t count,
                                 uint32_t *colors) __attribute__((weak));

static void ddraw_destroy_palette(my_palette_t *pal)
{
    if (!pal)
        return;
    if (pal->rb_palette && rb_palette_destroy)
        rb_palette_destroy(pal->rb_palette);
    ddraw_free(pal);
}

static uint32_t ddraw_palette_color_count(uint32_t flags)
{
    if (flags & DDPCAPS_1BIT)
        return 2;
    if (flags & DDPCAPS_2BIT)
        return 4;
    if (flags & DDPCAPS_4BIT)
        return 16;
    return 256;
}

static void ddraw_palette_pack_colors(const DDPALETTEENTRY *entries,
                                      uint32_t count, uint32_t *colors)
{
    uint32_t i;

    for (i = 0; i < count; i++) {
        colors[i] = ((uint32_t)entries[i].peBlue << 16) |
                    ((uint32_t)entries[i].peGreen << 8) |
                    (uint32_t)entries[i].peRed;
    }
}

HRESULT ddraw_palette_create(my_dd_t *dd, uint32_t flags, void *ddpalette,
                             void **lplpDDPalette)
{
    my_palette_t *pal;
    rb_palette_t rb_pal;
    const DDPALETTEENTRY *entries = (const DDPALETTEENTRY *)ddpalette;
    uint32_t colors[256];
    uint32_t count;

    if (!dd || !lplpDDPalette)
        return DDERR_INVALIDPARAMS;
    if (!rb_palette_create)
        return DDERR_UNSUPPORTED;

    count = ddraw_palette_color_count(flags);
    rb_pal = rb_palette_create((int)count);
    if (!rb_pal)
        return DDERR_OUTOFMEMORY;

    pal = ddraw_alloc_mem(sizeof(*pal));
    if (!pal) {
        rb_palette_destroy(rb_pal);
        return DDERR_OUTOFMEMORY;
    }

    memset(pal, 0, sizeof(*pal));
    pal->lpVtbl = (IDirectDrawPaletteVtbl *)&palette_vtbl;
    pal->ref_count = 1;
    pal->rb_palette = rb_pal;
    pal->num_colors = count;
    pal->caps = flags;

    memset(colors, 0, sizeof(colors));
    if (entries) {
        ddraw_palette_pack_colors(entries, count, colors);
        rb_palette_set_colors(rb_pal, 0, count, colors);
    }

    *lplpDDPalette = FORCE_PTR_RETURN(pal);
    DEBUG("ddraw: CreatePalette flags=0x%x count=%u -> %p", flags, count, pal);
    return DD_OK;
}

static HRESULT KERNEL32_STUB palette_QueryInterface(void *this_ptr, const GUID *riid,
                                                    void **ppvObj)
{
    my_palette_t *pal = (my_palette_t *)this_ptr;

    if (!pal || !riid || !ppvObj)
        return DDERR_INVALIDPARAMS;

    *ppvObj = NULL;
    if (IsEqualGUID(riid, &IID_IDirectDrawPalette)) {
        pal->ref_count++;
        *ppvObj = FORCE_PTR_RETURN(pal);
        return DD_OK;
    }
    return DDERR_UNSUPPORTED;
}

uint32_t KERNEL32_STUB ddraw_palette_add_ref(my_palette_t *pal)
{
    if (pal)
        pal->ref_count++;
    return pal ? pal->ref_count : 0;
}

uint32_t KERNEL32_STUB ddraw_palette_release(my_palette_t *pal)
{
    if (!pal)
        return 0;

    if (pal->ref_count > 0)
        pal->ref_count--;
    if (pal->ref_count != 0)
        return pal->ref_count;

    ddraw_destroy_palette(pal);
    return 0;
}

static uint32_t KERNEL32_STUB palette_AddRef(void *this_ptr)
{
    return ddraw_palette_add_ref((my_palette_t *)this_ptr);
}

static uint32_t KERNEL32_STUB palette_Release(void *this_ptr)
{
    return ddraw_palette_release((my_palette_t *)this_ptr);
}

static HRESULT KERNEL32_STUB palette_GetCaps(void *this_ptr, uint32_t *lpdwCaps)
{
    my_palette_t *pal = (my_palette_t *)this_ptr;

    if (!pal || !lpdwCaps)
        return DDERR_INVALIDPARAMS;

    *lpdwCaps = pal->caps;
    return DD_OK;
}

static HRESULT KERNEL32_STUB palette_GetEntries(void *this_ptr, void *ddpba,
                                                uint32_t dwStart, uint32_t dwCount,
                                                void *ddpe)
{
    my_palette_t *pal = (my_palette_t *)this_ptr;
    DDPALETTEENTRY *entries = (DDPALETTEENTRY *)ddpe;
    uint32_t colors[256];
    uint32_t i;

    (void)ddpba;

    if (!pal || !entries || !rb_palette_get_colors)
        return DDERR_INVALIDPARAMS;
    if (dwCount > pal->num_colors)
        dwCount = pal->num_colors;
    if (rb_palette_get_colors(pal->rb_palette, dwStart, dwCount, colors) != RB_OK)
        return DDERR_INVALIDPARAMS;

    for (i = 0; i < dwCount; i++) {
        entries[i].peRed = (uint8_t)(colors[i] & 0xFF);
        entries[i].peGreen = (uint8_t)((colors[i] >> 8) & 0xFF);
        entries[i].peBlue = (uint8_t)((colors[i] >> 16) & 0xFF);
        entries[i].peFlags = 0;
    }
    return DD_OK;
}

static HRESULT KERNEL32_STUB palette_Initialize(void *this_ptr, void *lpDD,
                                                uint32_t dwFlags,
                                                void *lpDDColorTable)
{
    my_palette_t *pal = (my_palette_t *)this_ptr;

    (void)lpDD;
    (void)lpDDColorTable;

    if (!pal)
        return DDERR_INVALIDPARAMS;

    pal->caps = dwFlags;
    return DD_OK;
}

static HRESULT KERNEL32_STUB palette_SetEntries(void *this_ptr, void *ddpba,
                                                uint32_t dwStart, uint32_t dwCount,
                                                void *ddpe)
{
    my_palette_t *pal = (my_palette_t *)this_ptr;
    const DDPALETTEENTRY *entries = (const DDPALETTEENTRY *)ddpe;
    uint32_t colors[256];

    (void)ddpba;

    if (!pal || !entries || !rb_palette_set_colors)
        return DDERR_INVALIDPARAMS;
    ddraw_debug_counter("PaletteSetEntries");
    if (dwCount > pal->num_colors)
        dwCount = pal->num_colors;

    ddraw_palette_pack_colors(entries, dwCount, colors);
    return (rb_palette_set_colors(pal->rb_palette, dwStart, dwCount, colors) == RB_OK)
        ? DD_OK : DDERR_INVALIDPARAMS;
}

const IDirectDrawPaletteVtbl palette_vtbl = {
    .QueryInterface = palette_QueryInterface,
    .AddRef = palette_AddRef,
    .Release = palette_Release,
    .GetCaps = palette_GetCaps,
    .GetEntries = palette_GetEntries,
    .Initialize = palette_Initialize,
    .SetEntries = palette_SetEntries,
};
