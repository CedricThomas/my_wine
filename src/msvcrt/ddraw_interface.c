/*
 * ddraw_interface.c
 *
 * Minimal maintained DirectDraw path for Doom95:
 * - DirectDrawCreate + IDirectDraw core methods
 * - primary/backbuffer surface creation
 * - palette creation/update
 * - Lock/Unlock/GetSurfaceDesc/Flip/Blt/GetAttachedSurface/SetPalette
 */

#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "ddraw_priv.h"
#include "user32_priv.h"
#include "../syscall/syscalls_inline.h"

extern int rb_init(void) __attribute__((weak));
extern rb_window_t rb_window_create(const char *title, int x, int y, int w, int h,
                                    uint32_t flags) __attribute__((weak));
extern int rb_window_destroy(rb_window_t win) __attribute__((weak));
extern int rb_window_set_fullscreen(rb_window_t win, int fullscreen,
                                    int w, int h, int bpp) __attribute__((weak));
extern rb_surface_t rb_surface_create(int w, int h, rb_pixel_format_t format,
                                      rb_palette_t palette, uint32_t flags) __attribute__((weak));
extern rb_surface_t rb_surface_create_flip_chain(rb_window_t win, int w, int h,
                                                 rb_pixel_format_t format,
                                                 rb_palette_t palette,
                                                 int backbuffer_count) __attribute__((weak));
extern int rb_surface_destroy(rb_surface_t surf) __attribute__((weak));
extern int rb_surface_lock(rb_surface_t surf, const rb_rect_t *rect,
                           uint8_t **out_data, int *out_pitch) __attribute__((weak));
extern int rb_surface_unlock(rb_surface_t surf) __attribute__((weak));
extern int rb_surface_blt(rb_surface_t dst, const rb_rect_t *dst_rect,
                          rb_surface_t src, const rb_rect_t *src_rect,
                          uint32_t color, uint32_t flags) __attribute__((weak));
extern int rb_surface_flip(rb_surface_t surf) __attribute__((weak));
extern int rb_surface_get_desc(rb_surface_t surf, int *w, int *h,
                               rb_pixel_format_t *format, int *pitch) __attribute__((weak));
extern int rb_surface_set_palette(rb_surface_t surf, rb_palette_t pal) __attribute__((weak));
extern rb_palette_t rb_palette_create(int num_colors) __attribute__((weak));
extern int rb_palette_destroy(rb_palette_t pal) __attribute__((weak));
extern int rb_palette_set_colors(rb_palette_t pal, uint32_t start, uint32_t count,
                                 const uint32_t *colors) __attribute__((weak));
extern int rb_palette_get_colors(rb_palette_t pal, uint32_t start, uint32_t count,
                                 uint32_t *colors) __attribute__((weak));

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

typedef struct {
    uint32_t dwCaps;
} ddraw_guest_caps_t;

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

my_dd_t *g_ddraw_instance = NULL;

static int g_ddraw_backend_inited = 0;
static int g_ddraw_backend_available = 0;

static void ddraw_debug_counter(const char *tag)
{
    typedef struct {
        const char *tag;
        uint32_t count;
    } ddraw_debug_counter_slot;
    static ddraw_debug_counter_slot slots[16];
    int i;

    if (!debug_level_at_least(1) || !tag)
        return;

    for (i = 0; i < 16; i++) {
        if (slots[i].tag == tag || slots[i].tag == NULL) {
            uint32_t count;

            if (slots[i].tag == NULL)
                slots[i].tag = tag;
            count = ++slots[i].count;
            if ((count & (count - 1)) == 0 || (count % 100000u) == 0)
                DEBUG("ddraw: %s count=%u", tag, count);
            return;
        }
    }
}

static int ddraw_ensure_backend(void)
{
    if (!g_ddraw_backend_inited) {
        g_ddraw_backend_inited = 1;
        g_ddraw_backend_available = (rb_init && rb_init() == 0) ? 1 : 0;
    }
    return g_ddraw_backend_available;
}

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

static void ddraw_fill_surface_desc(my_surface_t *surf, ddraw_guest_desc_t *desc,
                                    void *surface_ptr)
{
    if (!surf || !desc)
        return;

    if (rb_surface_get_desc) {
        int width = 0;
        int height = 0;
        int pitch = 0;
        rb_pixel_format_t format;
        if (rb_surface_get_desc(surf->rb_surface, &width, &height, &format, &pitch) == RB_OK) {
            surf->width = (uint32_t)width;
            surf->height = (uint32_t)height;
            surf->pitch = pitch;
        }
    }

    memset(desc, 0, sizeof(*desc));
    desc->ddSize = sizeof(*desc);
    desc->ddFlags = DDSD_CAPS | DDSD_WIDTH | DDSD_HEIGHT | DDSD_PITCH;
    if (surface_ptr)
        desc->ddFlags |= DDSD_LPSURFACE;
    desc->ddsCaps.dwCaps = surf->caps;
    desc->lPitch = surf->pitch;
    desc->dwBackBufferCount = surf->next ? 1u : 0u;
    desc->dwWidth = surf->width;
    desc->dwHeight = surf->height;
    desc->lpSurface = (uint32_t)(uintptr_t)surface_ptr;
}

static ddraw_backend_window_state *ddraw_get_backend_window(rb_window_t win)
{
    if (!win || wine_handle_get_type((uint32_t)win) != HANDLE_TYPE_RB_WINDOW)
        return NULL;
    return (ddraw_backend_window_state *)wine_handle_get((uint32_t)win);
}

static my_surface_t *ddraw_alloc_surface(my_dd_t *dd, rb_surface_t rb_surface,
                                         uint32_t caps)
{
    my_surface_t *surf = ddraw_alloc_mem(sizeof(*surf));
    if (!surf)
        return NULL;

    memset(surf, 0, sizeof(*surf));
    surf->lpVtbl = (IDirectDrawSurfaceVtbl *)&surface_vtbl;
    surf->ref_count = 1;
    surf->rb_surface = rb_surface;
    surf->caps = caps;
    surf->dd_owner = dd;
    if (dd) {
        surf->owner_list_next = dd->surface_list;
        dd->surface_list = surf;
    }
    return surf;
}

static void ddraw_unlink_surface_from_owner(my_surface_t *surf)
{
    my_dd_t *dd;
    my_surface_t **link;

    if (!surf || !surf->dd_owner)
        return;

    dd = surf->dd_owner;
    link = &dd->surface_list;
    while (*link) {
        if (*link == surf) {
            *link = surf->owner_list_next;
            break;
        }
        link = &(*link)->owner_list_next;
    }

    surf->dd_owner = NULL;
    surf->owner_list_next = NULL;
}

static void ddraw_destroy_palette(my_palette_t *pal)
{
    if (!pal)
        return;
    if (pal->rb_palette && rb_palette_destroy)
        rb_palette_destroy(pal->rb_palette);
    ddraw_free(pal);
}

static uint32_t KERNEL32_STUB palette_Release(void *this_ptr);
static uint32_t KERNEL32_STUB surface_Release(void *this_ptr);
static uint32_t KERNEL32_STUB clipper_AddRef(void *this_ptr);
static uint32_t KERNEL32_STUB clipper_Release(void *this_ptr);

static void ddraw_destroy_surface(my_surface_t *surf)
{
    if (!surf)
        return;

    if (surf->next) {
        my_surface_t *attached = surf->next;
        surf->next = NULL;
        surface_Release(attached);
    }

    if (surf->palette) {
        my_palette_t *pal = surf->palette;
        surf->palette = NULL;
        palette_Release(pal);
    }

    ddraw_unlink_surface_from_owner(surf);

    if (surf->rb_surface && rb_surface_destroy)
        rb_surface_destroy(surf->rb_surface);

    ddraw_free(surf);
}

static HRESULT KERNEL32_STUB ddraw_DuplicateSurface(void *this_ptr, void *lpDDSurface,
                                                    void **lplpDupDDSurface)
{
    (void)this_ptr;
    (void)lpDDSurface;
    (void)lplpDupDDSurface;
    return DDERR_UNSUPPORTED;
}

static HRESULT KERNEL32_STUB ddraw_QueryInterface(void *this_ptr, const GUID *riid, void **ppvObj)
{
    my_dd_t *dd = (my_dd_t *)this_ptr;

    if (!dd || !riid || !ppvObj)
        return DDERR_INVALIDPARAMS;

    *ppvObj = NULL;
    if (IsEqualGUID(riid, &IID_IDirectDraw)) {
        dd->ref_count++;
        *ppvObj = FORCE_PTR_RETURN(dd);
        return DD_OK;
    }
    return DDERR_UNSUPPORTED;
}

static uint32_t KERNEL32_STUB ddraw_AddRef(void *this_ptr)
{
    my_dd_t *dd = (my_dd_t *)this_ptr;
    if (dd)
        dd->ref_count++;
    return dd ? dd->ref_count : 0;
}

static uint32_t KERNEL32_STUB ddraw_Release(void *this_ptr)
{
    my_dd_t *dd = (my_dd_t *)this_ptr;
    my_surface_t *surf;

    if (!dd)
        return 0;

    if (dd->ref_count > 0)
        dd->ref_count--;
    if (dd->ref_count != 0)
        return dd->ref_count;

    surf = dd->surface_list;
    while (surf) {
        my_surface_t *next = surf->owner_list_next;
        surf->dd_owner = NULL;
        surf->owner_list_next = NULL;
        surf = next;
    }
    dd->surface_list = NULL;

    if (dd->primary_surface) {
        my_surface_t *primary = dd->primary_surface;
        dd->primary_surface = NULL;
        surface_Release(primary);
    }

    if (dd->owns_window && dd->rb_window && rb_window_destroy)
        rb_window_destroy(dd->rb_window);

    if (g_ddraw_instance == dd)
        g_ddraw_instance = NULL;
    ddraw_free(dd);
    return 0;
}

static HRESULT KERNEL32_STUB ddraw_Compact(void *this_ptr) { (void)this_ptr; return DDERR_UNSUPPORTED; }
static HRESULT KERNEL32_STUB ddraw_CreateClipper(void *this_ptr, uint32_t flags, void **lplpClipper, void *unk)
{
    my_dd_t *dd = (my_dd_t *)this_ptr;
    my_clipper_t *clipper;

    (void)flags;
    (void)unk;

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
static HRESULT KERNEL32_STUB ddraw_FlipToGDISurface(void *this_ptr) { (void)this_ptr; return DDERR_UNSUPPORTED; }
static HRESULT KERNEL32_STUB ddraw_WaitForVerticalBlank(void *this_ptr, uint32_t flags, void *hEvent)
{
    struct timespec ts;

    (void)this_ptr;
    (void)flags;
    (void)hEvent;

    ddraw_debug_counter("WaitForVerticalBlank");
    ts.tv_sec = 0;
    ts.tv_nsec = 1000000L;
    (void)INLINE_SYSCALL_NANOSLEEP(&ts, NULL);
    return DD_OK;
}

static HRESULT KERNEL32_STUB ddraw_GetMonitorFrequency(void *this_ptr, uint32_t *dwFreq)
{
    (void)this_ptr;
    ddraw_debug_counter("GetMonitorFrequency");
    if (dwFreq)
        *dwFreq = 60;
    return DD_OK;
}

static HRESULT KERNEL32_STUB ddraw_GetScanLine(void *this_ptr, uint32_t *dwScanLine)
{
    static uint32_t fake_scanline = 0;

    (void)this_ptr;
    ddraw_debug_counter("GetScanLine");
    if (dwScanLine)
        *dwScanLine = (fake_scanline++) % 449u;
    return DD_OK;
}

static HRESULT KERNEL32_STUB ddraw_GetVerticalBlankStatus(void *this_ptr, int *lpInVerticalBlank)
{
    static uint32_t poll_count = 0;
    static int fake_in_vblank = 0;
    uint32_t count;
    struct timespec ts;

    (void)this_ptr;
    ddraw_debug_counter("GetVerticalBlankStatus");
    count = ++poll_count;
    fake_in_vblank ^= 1;

    if ((count & 63u) == 0u) {
        ts.tv_sec = 0;
        ts.tv_nsec = 1000000L;
        (void)INLINE_SYSCALL_NANOSLEEP(&ts, NULL);
    }
    if (lpInVerticalBlank)
        *lpInVerticalBlank = fake_in_vblank;
    return DD_OK;
}

static HRESULT KERNEL32_STUB ddraw_GetFourCCCodes(void *this_ptr, uint32_t *lpNumCodes,
                                                  uint32_t *lpCodes)
{
    (void)this_ptr;
    if (lpNumCodes)
        *lpNumCodes = 0;
    (void)lpCodes;
    return DD_OK;
}

static HRESULT KERNEL32_STUB ddraw_GetGDISurface(void *this_ptr, void **lpSurface)
{
    my_dd_t *dd = (my_dd_t *)this_ptr;

    if (!dd || !lpSurface)
        return DDERR_INVALIDPARAMS;

    *lpSurface = NULL;
    if (!dd->primary_surface)
        return DDERR_NOTFOUND;

    dd->primary_surface->ref_count++;
    *lpSurface = FORCE_PTR_RETURN(dd->primary_surface);
    return DD_OK;
}

static HRESULT KERNEL32_STUB ddraw_Initialize(void *this_ptr, GUID *lpGUID)
{
    (void)this_ptr;
    (void)lpGUID;
    return DD_OK;
}

static HRESULT KERNEL32_STUB ddraw_EnumDisplayModes(void *this_ptr, uint32_t dwFlags, void *ddsd,
                                                    void *lpContext, void *lpEnumCallback)
{
    (void)this_ptr;
    (void)dwFlags;
    (void)ddsd;
    (void)lpContext;
    (void)lpEnumCallback;
    return DDERR_UNSUPPORTED;
}

static HRESULT KERNEL32_STUB ddraw_EnumSurfaces(void *this_ptr, uint32_t dwFlags, void *ddsd,
                                                void *lpContext, void *lpEnumCallback)
{
    (void)this_ptr;
    (void)dwFlags;
    (void)ddsd;
    (void)lpContext;
    (void)lpEnumCallback;
    return DDERR_UNSUPPORTED;
}

static HRESULT KERNEL32_STUB ddraw_GetDisplayMode(void *this_ptr, void *ddsd)
{
    my_dd_t *dd = (my_dd_t *)this_ptr;
    ddraw_guest_desc_t *desc = (ddraw_guest_desc_t *)ddsd;

    if (!dd || !desc)
        return DDERR_INVALIDPARAMS;

    memset(desc, 0, sizeof(*desc));
    desc->ddSize = sizeof(*desc);
    desc->ddFlags = DDSD_WIDTH | DDSD_HEIGHT;
    desc->ddsCaps.dwCaps = DDSCAPS_PRIMARYSURFACE;
    desc->dwWidth = dd->current_mode_w;
    desc->dwHeight = dd->current_mode_h;
    desc->lpSurface = 0;
    return DD_OK;
}

static HRESULT KERNEL32_STUB ddraw_RestoreDisplayMode(void *this_ptr)
{
    (void)this_ptr;
    return DD_OK;
}

static HRESULT KERNEL32_STUB ddraw_SetCooperativeLevel(void *this_ptr, void *hwnd, uint32_t flags)
{
    my_dd_t *dd = (my_dd_t *)this_ptr;
    wine_window_entry *entry;

    if (!dd)
        return DDERR_INVALIDPARAMS;

    dd->cooperative_hwnd = hwnd;
    dd->cooperative_level = flags;
    dd->is_exclusive = (flags & (DDSCL_FULLSCREEN | DDSCL_EXCLUSIVE)) ? 1 : 0;

    entry = get_window_entry((HWND)hwnd);
    if (entry)
        dd->rb_window = entry->sdl_window;

    DEBUG("ddraw: SetCooperativeLevel hwnd=%p flags=0x%x rb_window=%u", hwnd, flags,
          (unsigned int)dd->rb_window);
    return DD_OK;
}

static HRESULT KERNEL32_STUB ddraw_SetDisplayMode(void *this_ptr, uint32_t width,
                                                  uint32_t height, uint32_t bpp)
{
    my_dd_t *dd = (my_dd_t *)this_ptr;

    if (!dd)
        return DDERR_INVALIDPARAMS;
    if (!dd->cooperative_hwnd)
        dd->cooperative_level = DDSCL_NORMAL;
    if (!ddraw_ensure_backend())
        return DDERR_UNSUPPORTED;

    dd->current_mode_w = width;
    dd->current_mode_h = height;
    dd->current_mode_bpp = bpp;

    if (!dd->rb_window && rb_window_create) {
        dd->rb_window = rb_window_create("DirectDraw", -1, -1,
                                         (int)width, (int)height, 0);
        if (dd->rb_window)
            dd->owns_window = 1;
    }

    if (dd->rb_window && rb_window_set_fullscreen)
        rb_window_set_fullscreen(dd->rb_window, dd->is_exclusive,
                                 (int)width, (int)height, (int)bpp);

    DEBUG("ddraw: SetDisplayMode %ux%ux%u exclusive=%d rb_window=%u", width, height, bpp,
          dd->is_exclusive, (unsigned int)dd->rb_window);
    return DD_OK;
}

static HRESULT KERNEL32_STUB ddraw_CreateSurface(void *this_ptr, void *ddsd,
                                                 void **lplpDDSurface, void *unk)
{
    my_dd_t *dd = (my_dd_t *)this_ptr;
    ddraw_guest_desc_t *desc = (ddraw_guest_desc_t *)ddsd;
    my_surface_t *primary = NULL;
    my_surface_t *backbuffer = NULL;
    rb_surface_t rb_primary = 0;
    rb_surface_t rb_backbuffer = 0;
    uint32_t caps;

    (void)unk;

    if (!dd || !desc || !lplpDDSurface)
        return DDERR_INVALIDPARAMS;
    if (!ddraw_ensure_backend())
        return DDERR_UNSUPPORTED;

    caps = desc->ddsCaps.dwCaps;
    if (desc->dwWidth == 0)
        desc->dwWidth = dd->current_mode_w;
    if (desc->dwHeight == 0)
        desc->dwHeight = dd->current_mode_h;

    if ((caps & DDSCAPS_PRIMARYSURFACE) && !dd->rb_window)
        return DDERR_NOCOOPERATIVELEVELSET;

    DEBUG("ddraw: CreateSurface caps=0x%x size=%ux%u backbuffers=%u",
          caps, desc->dwWidth, desc->dwHeight, desc->dwBackBufferCount);

    if ((caps & DDSCAPS_PRIMARYSURFACE) && (caps & DDSCAPS_FLIP)) {
        ddraw_backend_window_state *wnd;

        if (!rb_surface_create_flip_chain)
            return DDERR_UNSUPPORTED;

        rb_primary = rb_surface_create_flip_chain(dd->rb_window, (int)desc->dwWidth,
                                                  (int)desc->dwHeight,
                                                  ddraw_bpp_to_format(dd->current_mode_bpp),
                                                  0, (int)desc->dwBackBufferCount);
        if (!rb_primary)
            return DDERR_OUTOFMEMORY;

        primary = ddraw_alloc_surface(dd, rb_primary, caps);
        if (!primary)
            goto out_of_memory;

        wnd = ddraw_get_backend_window(dd->rb_window);
        rb_backbuffer = wnd ? wnd->backbuffer : 0;
        DEBUG("ddraw: flip chain primary=%u wnd=%p backend_backbuffer=%u",
              (unsigned int)rb_primary, (void *)wnd, (unsigned int)rb_backbuffer);
        if (!rb_backbuffer && wnd && rb_surface_create) {
            rb_backbuffer = rb_surface_create((int)desc->dwWidth, (int)desc->dwHeight,
                                              ddraw_bpp_to_format(dd->current_mode_bpp),
                                              0, RB_SURFACE_BACK);
            if (rb_backbuffer)
                wnd->backbuffer = rb_backbuffer;
            DEBUG("ddraw: created fallback backbuffer=%u", (unsigned int)rb_backbuffer);
        }
        if (rb_backbuffer) {
            backbuffer = ddraw_alloc_surface(dd, rb_backbuffer,
                                             DDSCAPS_BACKBUFFER | DDSCAPS_FLIP);
            if (!backbuffer)
                goto out_of_memory;
            primary->next = backbuffer;
        }

        dd->primary_surface = primary;
        primary->ref_count++;
    } else {
        uint32_t rb_flags = 0;

        if (caps & DDSCAPS_PRIMARYSURFACE)
            rb_flags |= RB_SURFACE_PRIMARY;
        if (caps & DDSCAPS_OFFSCREENPLAIN)
            rb_flags |= RB_SURFACE_OFFSCREEN;

        if (!rb_surface_create)
            return DDERR_UNSUPPORTED;

        rb_primary = rb_surface_create((int)desc->dwWidth, (int)desc->dwHeight,
                                       ddraw_bpp_to_format(dd->current_mode_bpp),
                                       0, rb_flags);
        if (!rb_primary)
            return DDERR_OUTOFMEMORY;

        primary = ddraw_alloc_surface(dd, rb_primary, caps);
        if (!primary)
            goto out_of_memory;

        if (caps & DDSCAPS_PRIMARYSURFACE) {
            dd->primary_surface = primary;
            primary->ref_count++;
        }
    }

    primary->width = desc->dwWidth;
    primary->height = desc->dwHeight;
    primary->pitch = (int32_t)desc->dwWidth;
    if (backbuffer) {
        backbuffer->width = desc->dwWidth;
        backbuffer->height = desc->dwHeight;
        backbuffer->pitch = (int32_t)desc->dwWidth;
    }

    *lplpDDSurface = FORCE_PTR_RETURN(primary);
    return DD_OK;

out_of_memory:
    if (backbuffer)
        ddraw_free(backbuffer);
    if (primary)
        ddraw_free(primary);
    if (rb_primary && rb_surface_destroy)
        rb_surface_destroy(rb_primary);
    return DDERR_OUTOFMEMORY;
}

static HRESULT KERNEL32_STUB ddraw_CreatePalette(void *this_ptr, uint32_t flags,
                                                 void *ddpalette, void **lplpDDPalette,
                                                 void *unk)
{
    my_dd_t *dd = (my_dd_t *)this_ptr;
    my_palette_t *pal;
    rb_palette_t rb_pal;
    const DDPALETTEENTRY *entries = (const DDPALETTEENTRY *)ddpalette;
    uint32_t colors[256];
    uint32_t count = 256;

    (void)unk;

    if (!dd || !lplpDDPalette)
        return DDERR_INVALIDPARAMS;
    if (!ddraw_ensure_backend() || !rb_palette_create)
        return DDERR_UNSUPPORTED;

    if (flags & DDPCAPS_1BIT)
        count = 2;
    else if (flags & DDPCAPS_2BIT)
        count = 4;
    else if (flags & DDPCAPS_4BIT)
        count = 16;
    else if (flags & DDPCAPS_8BIT)
        count = 256;

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
        uint32_t i;
        for (i = 0; i < count; i++) {
            colors[i] = ((uint32_t)entries[i].peBlue << 16) |
                        ((uint32_t)entries[i].peGreen << 8) |
                        (uint32_t)entries[i].peRed;
        }
        rb_palette_set_colors(rb_pal, 0, count, colors);
    }

    *lplpDDPalette = FORCE_PTR_RETURN(pal);
    DEBUG("ddraw: CreatePalette flags=0x%x count=%u -> %p", flags, count, pal);
    return DD_OK;
}

static HRESULT KERNEL32_STUB ddraw_GetCaps(void *this_ptr, void *ddcaps1, void *ddcaps2)
{
    my_dd_t *dd = (my_dd_t *)this_ptr;
    DDCAPS *caps;

    if (!dd)
        return DDERR_INVALIDPARAMS;

    if (ddcaps1) {
        caps = (DDCAPS *)ddcaps1;
        memset(caps, 0, sizeof(*caps));
        caps->dwSize = sizeof(*caps);
        caps->dwCaps = DDCAPS_BLT | DDCAPS_BLTHW | DDCAPS_PALETTE |
                       DDCAPS_PALETTE8 | DDCAPS_FLIP | DDCAPS_COMPLEX |
                       DDCAPS_FULLSCREENFLIP | DDCAPS_FULLSCREEN;
        caps->dwPaletteEntries = 256;
    }

    if (ddcaps2) {
        caps = (DDCAPS *)ddcaps2;
        memset(caps, 0, sizeof(*caps));
        caps->dwSize = sizeof(*caps);
    }

    return DD_OK;
}

const IDirectDrawVtbl ddraw_vtbl = {
    .QueryInterface = ddraw_QueryInterface,
    .AddRef = ddraw_AddRef,
    .Release = ddraw_Release,
    .Compact = ddraw_Compact,
    .CreateClipper = ddraw_CreateClipper,
    .CreatePalette = ddraw_CreatePalette,
    .CreateSurface = ddraw_CreateSurface,
    .DuplicateSurface = ddraw_DuplicateSurface,
    .EnumDisplayModes = ddraw_EnumDisplayModes,
    .EnumSurfaces = ddraw_EnumSurfaces,
    .FlipToGDISurface = ddraw_FlipToGDISurface,
    .GetCaps = ddraw_GetCaps,
    .GetDisplayMode = ddraw_GetDisplayMode,
    .GetFourCCCodes = ddraw_GetFourCCCodes,
    .GetGDISurface = ddraw_GetGDISurface,
    .GetMonitorFrequency = ddraw_GetMonitorFrequency,
    .GetScanLine = ddraw_GetScanLine,
    .GetVerticalBlankStatus = ddraw_GetVerticalBlankStatus,
    .Initialize = ddraw_Initialize,
    .RestoreDisplayMode = ddraw_RestoreDisplayMode,
    .SetCooperativeLevel = ddraw_SetCooperativeLevel,
    .SetDisplayMode = ddraw_SetDisplayMode,
    .WaitForVerticalBlank = ddraw_WaitForVerticalBlank,
};

static HRESULT KERNEL32_STUB surface_QueryInterface(void *this_ptr, const GUID *riid, void **ppvObj)
{
    my_surface_t *surf = (my_surface_t *)this_ptr;

    if (!surf || !riid || !ppvObj)
        return DDERR_INVALIDPARAMS;

    *ppvObj = NULL;
    if (IsEqualGUID(riid, &IID_IDirectDrawSurface)) {
        surf->ref_count++;
        *ppvObj = FORCE_PTR_RETURN(surf);
        return DD_OK;
    }
    return DDERR_UNSUPPORTED;
}

static uint32_t KERNEL32_STUB surface_AddRef(void *this_ptr)
{
    my_surface_t *surf = (my_surface_t *)this_ptr;
    if (surf)
        surf->ref_count++;
    return surf ? surf->ref_count : 0;
}

static uint32_t KERNEL32_STUB surface_Release(void *this_ptr)
{
    my_surface_t *surf = (my_surface_t *)this_ptr;

    if (!surf)
        return 0;

    if (surf->ref_count > 0)
        surf->ref_count--;
    if (surf->ref_count != 0)
        return surf->ref_count;

    if (surf->dd_owner && surf->dd_owner->primary_surface == surf)
        surf->dd_owner->primary_surface = NULL;
    ddraw_destroy_surface(surf);
    return 0;
}

static HRESULT KERNEL32_STUB surface_AddAttachedSurface(void *this_ptr, void *lpDDS)
{
    my_surface_t *surf = (my_surface_t *)this_ptr;
    my_surface_t *attached = (my_surface_t *)lpDDS;

    if (!surf || !attached)
        return DDERR_INVALIDPARAMS;
    if (surf->next)
        return DDERR_SURFACEALREADYATTACHED;

    attached->ref_count++;
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

static HRESULT KERNEL32_STUB surface_DeleteAttachedSurface(void *this_ptr, uint32_t dwFlags, void *lpDDS)
{
    my_surface_t *surf = (my_surface_t *)this_ptr;
    my_surface_t *attached = (my_surface_t *)lpDDS;

    (void)dwFlags;

    if (!surf || !attached || surf->next != attached)
        return DDERR_INVALIDPARAMS;

    surf->next = NULL;
    surface_Release(attached);
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
        surf->next->ref_count++;
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

static HRESULT KERNEL32_STUB surface_GetDC(void *this_ptr, void **lphDC) { (void)this_ptr; (void)lphDC; return DDERR_UNSUPPORTED; }
static HRESULT KERNEL32_STUB surface_GetFlipStatus(void *this_ptr, uint32_t dwFlags)
{
    (void)this_ptr;
    (void)dwFlags;
    ddraw_debug_counter("GetFlipStatus");
    return DD_OK;
}
static HRESULT KERNEL32_STUB surface_GetOverlayPosition(void *this_ptr, int32_t *lpl, int32_t *lpt) { (void)this_ptr; (void)lpl; (void)lpt; return DDERR_UNSUPPORTED; }

static HRESULT KERNEL32_STUB surface_GetPalette(void *this_ptr, void **lppPalette)
{
    my_surface_t *surf = (my_surface_t *)this_ptr;

    if (!surf || !lppPalette)
        return DDERR_INVALIDPARAMS;

    *lppPalette = NULL;
    if (!surf->palette)
        return DDERR_NOTFOUND;

    surf->palette->ref_count++;
    *lppPalette = FORCE_PTR_RETURN(surf->palette);
    return DD_OK;
}

static HRESULT KERNEL32_STUB surface_GetSurfaceDesc(void *this_ptr, void *lpDDSurfaceDesc)
{
    my_surface_t *surf = (my_surface_t *)this_ptr;

    if (!surf || !lpDDSurfaceDesc)
        return DDERR_INVALIDPARAMS;

    ddraw_fill_surface_desc(surf, (ddraw_guest_desc_t *)lpDDSurfaceDesc, NULL);
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
    ddraw_guest_desc_t *desc = (ddraw_guest_desc_t *)lpDDSurfaceDesc;

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
    if (desc)
        ddraw_fill_surface_desc(surf, desc, data);

    return DD_OK;
}

static HRESULT KERNEL32_STUB surface_ReleaseDC(void *this_ptr, void *hDC) { (void)this_ptr; (void)hDC; return DDERR_UNSUPPORTED; }
static HRESULT KERNEL32_STUB surface_Restore(void *this_ptr) { (void)this_ptr; return DD_OK; }

static HRESULT KERNEL32_STUB surface_SetClipper(void *this_ptr, void *lpDDClipper)
{
    my_surface_t *surf = (my_surface_t *)this_ptr;
    my_clipper_t *clipper = (my_clipper_t *)lpDDClipper;
    if (!surf)
        return DDERR_INVALIDPARAMS;

    if (clipper == surf->clipper)
        return DD_OK;

    if (surf->clipper)
        clipper_Release(surf->clipper);

    surf->clipper = clipper;
    if (clipper)
        clipper_AddRef(clipper);

    DEBUG("ddraw: surface SetClipper surface=%p clipper=%p", surf, clipper);
    return DD_OK;
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
        palette_Release(surf->palette);
    surf->palette = pal;
    if (pal) {
        pal->ref_count++;
        ddraw_debug_counter("SetPalette");
        if (rb_surface_set_palette && rb_surface_set_palette(surf->rb_surface, pal->rb_palette) != RB_OK)
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

    if (!surf || !lppDDClipper)
        return DDERR_INVALIDPARAMS;
    if (!surf->clipper) {
        *lppDDClipper = NULL;
        return DDERR_NOCLIPPERATTACHED;
    }

    clipper_AddRef(surf->clipper);
    *lppDDClipper = FORCE_PTR_RETURN(surf->clipper);
    return DD_OK;
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

static HRESULT KERNEL32_STUB palette_QueryInterface(void *this_ptr, const GUID *riid, void **ppvObj)
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

static uint32_t KERNEL32_STUB palette_AddRef(void *this_ptr)
{
    my_palette_t *pal = (my_palette_t *)this_ptr;
    if (pal)
        pal->ref_count++;
    return pal ? pal->ref_count : 0;
}

static uint32_t KERNEL32_STUB palette_Release(void *this_ptr)
{
    my_palette_t *pal = (my_palette_t *)this_ptr;

    if (!pal)
        return 0;

    if (pal->ref_count > 0)
        pal->ref_count--;
    if (pal->ref_count != 0)
        return pal->ref_count;

    ddraw_destroy_palette(pal);
    return 0;
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
    uint32_t i;

    (void)ddpba;

    if (!pal || !entries || !rb_palette_set_colors)
        return DDERR_INVALIDPARAMS;
    ddraw_debug_counter("PaletteSetEntries");
    if (dwCount > pal->num_colors)
        dwCount = pal->num_colors;

    for (i = 0; i < dwCount; i++) {
        colors[i] = ((uint32_t)entries[i].peBlue << 16) |
                    ((uint32_t)entries[i].peGreen << 8) |
                    (uint32_t)entries[i].peRed;
    }

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

static HRESULT KERNEL32_STUB clipper_QueryInterface(void *this_ptr, const GUID *riid, void **ppvObj)
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

static uint32_t KERNEL32_STUB clipper_AddRef(void *this_ptr) { my_clipper_t *clipper = (my_clipper_t *)this_ptr; if (clipper) clipper->ref_count++; return clipper ? clipper->ref_count : 0; }
static uint32_t KERNEL32_STUB clipper_Release(void *this_ptr) { my_clipper_t *clipper = (my_clipper_t *)this_ptr; uint32_t ref_count; if (!clipper) return 0; if (clipper->ref_count > 0) clipper->ref_count--; ref_count = clipper->ref_count; if (ref_count == 0) ddraw_free(clipper); return ref_count; }
static HRESULT KERNEL32_STUB clipper_SetHWnd(void *this_ptr, uint32_t flags, void *hWnd) { my_clipper_t *clipper = (my_clipper_t *)this_ptr; (void)flags; if (!clipper) return DDERR_INVALIDPARAMS; clipper->hWnd = hWnd; return DD_OK; }
static HRESULT KERNEL32_STUB clipper_GetHWnd(void *this_ptr, void **lphWnd) { my_clipper_t *clipper = (my_clipper_t *)this_ptr; if (!clipper || !lphWnd) return DDERR_INVALIDPARAMS; *lphWnd = FORCE_PTR_RETURN(clipper->hWnd); return DD_OK; }
static HRESULT KERNEL32_STUB clipper_SetClipList(void *this_ptr, void *lpClipList, void *hWnd) { (void)this_ptr; (void)lpClipList; (void)hWnd; return DDERR_UNSUPPORTED; }
static HRESULT KERNEL32_STUB clipper_GetClipList(void *this_ptr, void *lpClipList, void *hWnd) { (void)this_ptr; (void)lpClipList; (void)hWnd; return DDERR_UNSUPPORTED; }
static HRESULT KERNEL32_STUB clipper_IsClipListChanged(void *this_ptr) { (void)this_ptr; return DD_FALSE; }

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

HRESULT KERNEL32_STUB DirectDrawCreate(const GUID *lpGUID, LPDIRECTDRAW *lplpDD,
                                       void *pUnkOuter)
{
    my_dd_t *dd;

    (void)lpGUID;

    if (!lplpDD)
        return DDERR_INVALIDPARAMS;
    if (pUnkOuter)
        return CLASS_E_NOAGGREGATION;

    dd = ddraw_alloc_mem(sizeof(*dd));
    if (!dd)
        return DDERR_OUTOFMEMORY;

    memset(dd, 0, sizeof(*dd));
    dd->lpVtbl = (IDirectDrawVtbl *)&ddraw_vtbl;
    dd->ref_count = 1;
    dd->current_mode_w = 320;
    dd->current_mode_h = 200;
    dd->current_mode_bpp = 8;
    dd->cooperative_level = DDSCL_NORMAL;

    g_ddraw_instance = dd;
    *lplpDD = FORCE_PTR_RETURN(dd);
    DEBUG("ddraw: DirectDrawCreate guid=%p -> %p", lpGUID, dd);
    return DD_OK;
}
