/*
 * ddraw_mode.c
 *
 * Internal DirectDraw mode/query helper support:
 * - fake vertical-blank and scanline query behavior
 * - caps/display-mode/GDI-surface query helpers
 */

#include <string.h>
#include <time.h>

#include "ddraw_priv.h"
#include "../syscall/syscalls_inline.h"

/*
 * Keep guest pointer validation local to the query helpers that write through
 * guest-supplied pointers.
 */
static inline int ddraw_guest_ptr_valid(const void *ptr)
{
    return (uintptr_t)ptr > 0x10000;
}

HRESULT ddraw_wait_for_vertical_blank(void)
{
    struct timespec ts;

    ddraw_debug_counter("WaitForVerticalBlank");
    ts.tv_sec = 0;
    ts.tv_nsec = 1000000L;
    (void)INLINE_SYSCALL_NANOSLEEP(&ts, NULL);
    return DD_OK;
}

HRESULT ddraw_get_monitor_frequency(uint32_t *dwFreq)
{
    ddraw_debug_counter("GetMonitorFrequency");
    if (ddraw_guest_ptr_valid(dwFreq))
        *dwFreq = 60;
    return DD_OK;
}

HRESULT ddraw_get_scan_line(uint32_t *dwScanLine)
{
    static uint32_t fake_scanline = 0;

    ddraw_debug_counter("GetScanLine");
    if (dwScanLine)
        *dwScanLine = (fake_scanline++) % 449u;
    return DD_OK;
}

HRESULT ddraw_get_vertical_blank_status(int *lpInVerticalBlank)
{
    static uint32_t poll_count = 0;
    static int fake_in_vblank = 0;
    uint32_t count;
    struct timespec ts;

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

HRESULT ddraw_get_fourcc_codes(uint32_t *lpNumCodes, uint32_t *lpCodes)
{
    if (ddraw_guest_ptr_valid(lpNumCodes))
        *lpNumCodes = 0;
    if (lpCodes && !ddraw_guest_ptr_valid(lpCodes))
        return DDERR_INVALIDPARAMS;
    (void)lpCodes;
    return DD_OK;
}

HRESULT ddraw_get_gdi_surface(my_dd_t *dd, void **lpSurface)
{
    if (!dd)
        return DDERR_INVALIDPARAMS;
    if (!ddraw_guest_ptr_valid(lpSurface))
        return DDERR_INVALIDPARAMS;

    *lpSurface = NULL;
    if (!dd->primary_surface)
        return DDERR_NOTFOUND;

    ddraw_surface_add_ref(dd->primary_surface);
    *lpSurface = FORCE_PTR_RETURN(dd->primary_surface);
    return DD_OK;
}

HRESULT ddraw_get_display_mode(my_dd_t *dd, void *ddsd)
{
    if (!dd)
        return DDERR_INVALIDPARAMS;
    if (!ddraw_guest_ptr_valid(ddsd))
        return DDERR_INVALIDPARAMS;

    ddraw_init_display_mode_desc(ddsd, dd->current_mode_w, dd->current_mode_h,
                                 DDSCAPS_PRIMARYSURFACE);
    return DD_OK;
}

HRESULT ddraw_restore_display_mode(void)
{
    return DD_OK;
}

HRESULT ddraw_get_caps(my_dd_t *dd, void *ddcaps1, void *ddcaps2)
{
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
