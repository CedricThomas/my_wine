/*
 * ddraw_priv.h — Internal DirectDraw structures, vtable externs, and helpers.
 *
 * Private header used only by the DDraw stub implementation.  Defines the
 * concrete C structs that sit behind each COM interface pointer returned
 * to guest code.
 */

#ifndef MY_WINE_DDRAW_PRIV_H
#define MY_WINE_DDRAW_PRIV_H

#include "ddraw_types.h"
#include "wine_abi.h"
#include "render_backend.h"
#include "debug.h"
#include <stdlib.h>
#ifdef MY_WINE32
#include "include/kernel32.h"
#endif

/* ═══════════════════════════════════════════════════════════ */
/* ── Internal structs ─────────────────────────────────────── */
/* ═══════════════════════════════════════════════════════════ */

/*
 * my_dd_t — internal representation of the IDirectDraw object.
 *
 * The first member (lpVtbl) must be the vtable pointer so that a
 * cast to IDirectDraw* works transparently (C standard §6.7.2.1).
 */
typedef struct my_dd {
    IDirectDrawVtbl *lpVtbl;          /* COM vtable pointer            */
    uint32_t         ref_count;        /* COM reference count           */
    rb_window_t      rb_window;        /* underlying render backend win */
    int              owns_window;      /* created internally for DDraw  */
    void            *cooperative_hwnd; /* guest HWND from SetCooperativeLevel */
    uint32_t         cooperative_level;/* DDSCL_* flags                 */
    int              is_exclusive;     /* 1 if exclusive/fullscreen     */
    uint32_t         current_mode_w;   /* active display width          */
    uint32_t         current_mode_h;   /* active display height         */
    uint32_t         current_mode_bpp; /* active display bits-per-pixel */
    struct my_surface *primary_surface; /* primary surface handle        */
    struct my_surface *surface_list;   /* all surfaces created by this DD */
    struct my_palette *palette;        /* default palette               */
} my_dd_t;

/*
 * my_surface_t — internal representation of an IDirectDrawSurface.
 *
 * Doubly-linked flip chain via next (linked list through flip chain;
 * prev is tracked implicitly by the chain owner).
 */
typedef struct my_surface {
    IDirectDrawSurfaceVtbl *lpVtbl;   /* COM vtable pointer            */
    uint32_t        ref_count;         /* COM reference count           */
    rb_surface_t    rb_surface;        /* underlying render backend surf */
    uint32_t        caps;              /* DDSCAPS_* flags               */
    uint32_t        width;             /* pixel width                   */
    uint32_t        height;            /* pixel height                  */
    int32_t         pitch;             /* bytes per line (can be neg)   */
    int             locked;            /* surface is currently locked   */
    struct my_dd   *dd_owner;          /* owning my_dd_t                */
    struct my_surface *next;           /* next in flip chain (linked list) */
    struct my_surface *owner_list_next;/* next surface in dd->surface_list */
    struct my_palette *palette;        /* attached palette              */
    struct my_clipper *clipper;        /* attached clipper              */
} my_surface_t;

/*
 * my_palette_t — internal representation of an IDirectDrawPalette.
 */
typedef struct my_palette {
    IDirectDrawPaletteVtbl *lpVtbl;   /* COM vtable pointer            */
    uint32_t     ref_count;            /* COM reference count           */
    rb_palette_t rb_palette;           /* underlying render backend pal */
    uint32_t     num_colors;           /* number of palette entries     */
    uint32_t     caps;                 /* DDPCAPS_* creation flags      */
} my_palette_t;

/*
 * my_clipper_t — internal representation of an IDirectDrawClipper.
 */
typedef struct my_clipper {
    IDirectDrawClipperVtbl *lpVtbl;   /* COM vtable pointer            */
    uint32_t  ref_count;               /* COM reference count           */
    void     *hWnd;                    /* attached window handle        */
} my_clipper_t;

/* ═══════════════════════════════════════════════════════════ */
/* ── Vtable externs ───────────────────────────────────────── */
/* ═══════════════════════════════════════════════════════════ */

extern const IDirectDrawVtbl           ddraw_vtbl;
extern const IDirectDrawSurfaceVtbl    surface_vtbl;
extern const IDirectDrawPaletteVtbl    palette_vtbl;
extern const IDirectDrawClipperVtbl    clipper_vtbl;

/* ═══════════════════════════════════════════════════════════ */
/* ── Global state ─────────────────────────────────────────── */
/* ═══════════════════════════════════════════════════════════ */

extern my_dd_t *g_ddraw_instance;

/* ═══════════════════════════════════════════════════════════ */
/* ── Memory helpers ───────────────────────────────────────── */
/* ═══════════════════════════════════════════════════════════ */

/*
 * ddraw_alloc_mem — allocate memory for DDraw objects.
 *
 * When running under the 32-bit Wine ABI the host should use the
 * Windows heap (HeapAlloc) so that guest code which calls HeapFree
 * on a returned pointer works correctly.  Otherwise fall back to
 * the C library malloc.
 */
static inline void *ddraw_alloc_mem(size_t size)
{
#ifdef MY_WINE32
    /* Use the process heap — guest code may free this with HeapFree */
    void *heap = GetProcessHeap();
    if (!heap)
        return NULL;
    return HeapAlloc(heap, 0, size);
#else
    return malloc(size);
#endif
}

/*
 * ddraw_free — free memory allocated by ddraw_alloc_mem.
 */
static inline void ddraw_free(void *ptr)
{
    if (!ptr) return;
#ifdef MY_WINE32
    void *heap = GetProcessHeap();
    if (!heap)
        return;
    HeapFree(heap, 0, ptr);
#else
    free(ptr);
#endif
}

#endif /* MY_WINE_DDRAW_PRIV_H */
