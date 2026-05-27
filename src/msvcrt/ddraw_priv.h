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

my_dd_t *ddraw_create_instance(void);
HRESULT KERNEL32_STUB ddraw_query_interface(my_dd_t *dd, const GUID *riid,
                                            void **ppvObj);
uint32_t KERNEL32_STUB ddraw_add_ref(my_dd_t *dd);
uint32_t KERNEL32_STUB ddraw_release(my_dd_t *dd);
void ddraw_debug_counter(const char *tag);
HRESULT ddraw_palette_create(my_dd_t *dd, uint32_t flags, void *ddpalette,
                             void **lplpDDPalette);
uint32_t KERNEL32_STUB ddraw_palette_add_ref(my_palette_t *pal);
uint32_t KERNEL32_STUB ddraw_palette_release(my_palette_t *pal);
HRESULT ddraw_clipper_create(my_dd_t *dd, uint32_t flags, void **lplpClipper);
uint32_t KERNEL32_STUB ddraw_clipper_add_ref(my_clipper_t *clipper);
uint32_t KERNEL32_STUB ddraw_clipper_release(my_clipper_t *clipper);
void ddraw_surface_clear_clipper(my_surface_t *surf);
HRESULT KERNEL32_STUB ddraw_surface_set_clipper(my_surface_t *surf,
                                                my_clipper_t *clipper);
HRESULT KERNEL32_STUB ddraw_surface_get_clipper(my_surface_t *surf,
                                                void **lppDDClipper);
my_surface_t *ddraw_surface_alloc(my_dd_t *dd, rb_surface_t rb_surface,
                                  uint32_t caps);
uint32_t KERNEL32_STUB ddraw_surface_add_ref(my_surface_t *surf);
uint32_t KERNEL32_STUB ddraw_surface_release(my_surface_t *surf);
HRESULT ddraw_wait_for_vertical_blank(void);
HRESULT ddraw_get_monitor_frequency(uint32_t *dwFreq);
HRESULT ddraw_get_scan_line(uint32_t *dwScanLine);
HRESULT ddraw_get_vertical_blank_status(int *lpInVerticalBlank);
HRESULT ddraw_get_fourcc_codes(uint32_t *lpNumCodes, uint32_t *lpCodes);
HRESULT ddraw_get_gdi_surface(my_dd_t *dd, void **lpSurface);
HRESULT ddraw_get_display_mode(my_dd_t *dd, void *ddsd);
HRESULT ddraw_restore_display_mode(void);
HRESULT ddraw_get_caps(my_dd_t *dd, void *ddcaps1, void *ddcaps2);
int ddraw_ensure_backend(void);
HRESULT ddraw_create_flip_chain_surface(my_dd_t *dd, uint32_t width,
                                        uint32_t height, uint32_t backbuffers,
                                        uint32_t caps,
                                        my_surface_t **primary_out,
                                        my_surface_t **backbuffer_out,
                                        void **lplpDDSurface);
HRESULT ddraw_create_regular_surface(my_dd_t *dd, uint32_t width,
                                     uint32_t height, uint32_t caps,
                                     my_surface_t **primary_out,
                                     void **lplpDDSurface);

/* ═══════════════════════════════════════════════════════════ */
/* ── Surface-desc helpers ─────────────────────────────────── */
/* ═══════════════════════════════════════════════════════════ */

void ddraw_fill_surface_desc(my_surface_t *surf, void *guest_desc,
                             void *surface_ptr);
void ddraw_init_display_mode_desc(void *guest_desc, uint32_t width,
                                  uint32_t height, uint32_t caps);
void ddraw_parse_surface_desc(const void *guest_desc, uint32_t *caps,
                              uint32_t *width, uint32_t *height,
                              uint32_t *backbuffers);

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
