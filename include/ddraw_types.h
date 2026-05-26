/*
 * ddraw_types.h — DirectDraw 1.x COM types, structs, constants, and vtables.
 *
 * Self-contained header matching the DirectX 5 SDK layout for ddraw.dll.
 * All offsets and sizes must match the real COM layout.
 */

#ifndef MY_WINE_DDRAW_TYPES_H
#define MY_WINE_DDRAW_TYPES_H

#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include "wine_abi.h"

/* ═══════════════════════════════════════════════════════════ */
/* ── HRESULT ───────────────────────────────────────────────── */
/* ═══════════════════════════════════════════════════════════ */

typedef uint32_t HRESULT;

/* Standard return codes */
#define DD_OK                    ((HRESULT)0x00000000L)
#define DD_TRUE                  ((HRESULT)0x00000001L)
#define DD_FALSE                 ((HRESULT)0x00000000L)

/* DDraw facility error codes (FACILITY_DIRECTDRAW = 0x887) */
#define DDERR_GENERIC            ((HRESULT)0x88760001L)
#define DDERR_OUTOFMEMORY        ((HRESULT)0x88760002L)
#define DDERR_NOEMULATION        ((HRESULT)0x88760004L)
#define DDERR_COLORKEYNOTSET     ((HRESULT)0x88760005L)
#define DDERR_UNSUPPORTED        ((HRESULT)0x88760106L)
#define DDERR_INVALIDPARAMS      ((HRESULT)0x8876000DL)
#define DDERR_INVALIDOBJECT      ((HRESULT)0x8876000EL)
#define DDERR_DEVICEREMOVED      ((HRESULT)0x8876000FL)
#define DDERR_EXCEPTION          ((HRESULT)0x88760010L)
#define DDERR_BADSURFACE         ((HRESULT)0x88760012L)
#define DDERR_OPTIMIZEDOUT       ((HRESULT)0x88760013L)
#define DDERR_NOMIPMAPS          ((HRESULT)0x88760014L)
#define DDERR_NO3D               ((HRESULT)0x88760015L)
#define DDERR_NOVSYSMMEMORY      ((HRESULT)0x88760016L)
#define DDERR_NOZWBUFFER         ((HRESULT)0x88760017L)
#define DDERR_MAXEXTENDOVERLAP   ((HRESULT)0x88760018L)
#define DDERR_SURFACELOST        ((HRESULT)0x88760019L)
#define DDERR_SURFACEBUSY        ((HRESULT)0x8876001AL)
#define DDERR_UNSUPPORTEDMODE    ((HRESULT)0x8876001BL)
#define DDERR_NOLOCKING          ((HRESULT)0x8876001CL)
#define DDERR_DROPPED            ((HRESULT)0x8876001DL)
#define DDERR_EXCLUSIVEMODEALREADYSET ((HRESULT)0x8876001FL)
#define DDERR_NOFLIP             ((HRESULT)0x88760020L)
#define DDERR_OVERLAYNOTVISIBLE  ((HRESULT)0x88760021L)
#define DDERR_NOOVERLAYDEST      ((HRESULT)0x88760022L)
#define DDERR_OVERLAYNOTACTIVE   ((HRESULT)0x88760023L)
#define DDERR_PRIMARYSURFACEINCOMPAT ((HRESULT)0x88760024L)
#define DDERR_SURFACEISOBSCURED  ((HRESULT)0x88760025L)
#define DDERR_EXCEEDSSURFACELIMIT ((HRESULT)0x88760026L)
#define DDERR_MISMATCHEDVIDMEM   ((HRESULT)0x88760027L)
#define DDERR_UNSUPPORTEDMASK    ((HRESULT)0x88760029L)
#define DDERR_MISSINGINSTALLER   ((HRESULT)0x8876002AL)
#define DDERR_NOTALOCALDEVICE    ((HRESULT)0x8876002BL)
#define DDERR_NOTLOCKED          ((HRESULT)0x8876002CL)
#define DDERR_CANTDUPLICATE      ((HRESULT)0x8876002DL)
#define DDERR_NOTFOUND           ((HRESULT)0x8876002EL)
#define DDERR_MOREDATA           ((HRESULT)0x8876002FL)
#define DDERR_OVERLAPPINGRECTS   ((HRESULT)0x88760030L)
#define DDERR_DROPPEDEXECTHREAD  ((HRESULT)0x88760031L)
#define DDERR_CANCELLED          ((HRESULT)0x88760032L)
#define DDERR_INVALIDDIRECTDRAWGUID ((HRESULT)0x88760033L)
#define DDERR_DIRECTDRAWALREADYCREATED ((HRESULT)0x88760034L)
#define DDERR_NODIRECTDRAWHW     ((HRESULT)0x88760035L)
#define DDERR_NODIRECTDRAWSYSMEM ((HRESULT)0x88760036L)
#define DDERR_NODIRECTDRAVGPC    ((HRESULT)0x88760037L)
#define DDERR_NOCOOPERATIVELEVELSET ((HRESULT)0x88760038L)
#define DDERR_NODEVIDEOWINDOW    ((HRESULT)0x88760039L)
#define DDERR_NOPOTENTIALVIDEOWINDOW ((HRESULT)0x8876003AL)
#define DDERR_VIDEONOTACTIVE     ((HRESULT)0x8876003BL)
#define DDERR_NOMONITORINFO      ((HRESULT)0x8876003CL)
#define DDERR_LOWVERSION         ((HRESULT)0x8876003DL)
#define DDERR_GENERICERR         ((HRESULT)0x88760101L)
#define DDERR_NOEXCLUSIVEMODE    ((HRESULT)0x88760102L)
#define DDERR_NOTFLIPPABLE       ((HRESULT)0x88760103L)
#define DDERR_CANTCREATEDC       ((HRESULT)0x88760104L)
#define DDERR_NODC               ((HRESULT)0x88760105L)
#define DDERR_WASSTILLDRAWING    ((HRESULT)0x88760107L)
#define DDERR_OUTOFCOLLISIONDETECTION ((HRESULT)0x88760108L)
#define DDERR_EXPIRED            ((HRESULT)0x88760109L)
#define DDERR_INCOMPATIBLEPRIMARY ((HRESULT)0x8876010AL)
#define DDERR_INVALIDRECT        ((HRESULT)0x8876010BL)
#define DDERR_DESTINATIONOVERLAPPING ((HRESULT)0x8876010CL)
#define DDERR_SURFACEALREADYATTACHED ((HRESULT)0x8876010DL)
#define DDERR_SURFACEALREADYDEPENDENT ((HRESULT)0x8876010EL)
#define DDERR_CLIPPERISUSINGHWND ((HRESULT)0x8876010FL)
#define DDERR_NOCLIPPERATTACHED  ((HRESULT)0x88760110L)
#define DDERR_CLIPPERNOTACTIVE   ((HRESULT)0x88760111L)

/* COM class registration error */
#define CLASS_E_NOAGGREGATION    ((HRESULT)0x80040110L)

/* HRESULT factory macro — combines DDraw facility with a code */
#define MAKE_DDHRESULT(code)     ((HRESULT)(0x88760000L | (code)))

/* ═══════════════════════════════════════════════════════════ */
/* ── GUID layout ───────────────────────────────────────────── */
/* ═══════════════════════════════════════════════════════════ */

/* COM GUID layout: 16 bytes, little-endian fields */
typedef struct {
    uint32_t Data1;
    uint16_t Data2;
    uint16_t Data3;
    uint8_t  Data4[8];
} GUID;

/* CLSID_DirectDraw — the GUID used to create the DirectDraw object */
static const GUID CLSID_DirectDraw __attribute__((unused)) = {
    0x3C3A4F12, 0x0A7E, 0x11D2,
    { 0xA7, 0x62, 0x00, 0xA0, 0xC9, 0x2E, 0xE6, 0x30 }
};

/* IIDs for the four DirectDraw interfaces */
static const GUID IID_IDirectDraw __attribute__((unused)) = {
    0x00000003, 0x0000, 0x0000,
    { 0xC0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x46 }
};

static const GUID IID_IDirectDrawSurface __attribute__((unused)) = {
    0x00000004, 0x0000, 0x0000,
    { 0xC0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x46 }
};

static const GUID IID_IDirectDrawPalette __attribute__((unused)) = {
    0x00000009, 0x0000, 0x0000,
    { 0xC0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x46 }
};

static const GUID IID_IDirectDrawClipper __attribute__((unused)) = {
    0x0000000C, 0x0000, 0x0000,
    { 0xC0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x46 }
};

/* ═══════════════════════════════════════════════════════════ */
/* ── IsEqualGUID ───────────────────────────────────────────── */
/* ═══════════════════════════════════════════════════════════ */

static inline int IsEqualGUID(const GUID *rGuid1, const GUID *rGuid2)
{
    return memcmp(rGuid1, rGuid2, sizeof(GUID)) == 0;
}

/* ═══════════════════════════════════════════════════════════ */
/* ── DDSCAPS flags ──────────────────────────────────────────── */
/* ═══════════════════════════════════════════════════════════ */

#define DDSCAPS_ALPHA               0x00000002L
#define DDSCAPS_BACKBUFFER          0x00000004L
#define DDSCAPS_COMPLEX             0x00000008L
#define DDSCAPS_FLIP                0x00000010L
#define DDSCAPS_FRONTBUFFER         0x00000020L
#define DDSCAPS_OFFSCREENPLAIN      0x00000040L
#define DDSCAPS_OVERLAY             0x00000080L
#define DDSCAPS_PALETTE             0x00000100L
#define DDSCAPS_PRIMARYSURFACE      0x00000200L
#define DDSCAPS_PRIMARYSURFACELEFT  0x00000400L
#define DDSCAPS_SYSTEMMEMORY        0x00000800L
#define DDSCAPS_TEXTURE             0x00001000L
#define DDSCAPS_3DDEVICE            0x00002000L
#define DDSCAPS_VIDEOMEMORY         0x00004000L
#define DDSCAPS_VISIBLE             0x00008000L
#define DDSCAPS_RESERVED1           0x00000001L
#define DDSCAPS_WRITEONLY           0x00010000L
#define DDSCAPS_ZBUFFER             0x00020000L
#define DDSCAPS_OWNDC               0x00040000L
#define DDSCAPS_LIVEVIDEO           0x00080000L
#define DDSCAPS_HWCODEC             0x00100000L
#define DDSCAPS_MODEX               0x00200000L
#define DDSCAPS_MIPMAP              0x00400000L
#define DDSCAPS_RESERVED2           0x00800000L
#define DDSCAPS_ALLOCONLOAD         0x04000000L
#define DDSCAPS_VIDEOPORT           0x08000000L
#define DDSCAPS_LOCALVIDMEM         0x10000000L
#define DDSCAPS_REMOTEVIDMEM        0x20000000L
#define DDSCAPS_STANDARDVGAMODE     0x40000000L
#define DDSCAPS_OPTIMIZED           0x80000000L
#define DDSCAPS_LUIDBUFFER          0x00004000L
#define DDSCAPS_LLOVERRIDE          0x00008000L
#define DDSCAPS_MANAGED             0x00020000L
#define DDSCAPS_WRITEBLEND          0x80000000L
#define DDSCAPS_STENCIL             0x00800000L
#define DDSCAPS_AFFINEMATRIX        0x20000000L
#define DDSCAPS_STEREO              0x10000000L
#define DDSCAPS_OPTIMIZE            0x00400000L
#define DDSCAPS_PITCH               0x00800000L
#define DDSCAPS_NOVIDEOSIZE         0x08000000L
#define DDSCAPS_COLORKEY            0x00010000L

/* ═══════════════════════════════════════════════════════════ */
/* ── DDSD_* surface-desc flags ─────────────────────────────── */
/* ═══════════════════════════════════════════════════════════ */

#define DDSD_CAPS               0x00000001L
#define DDSD_HEIGHT             0x00000002L
#define DDSD_WIDTH              0x00000004L
#define DDSD_PITCH              0x00000008L
#define DDSD_BACKBUFFERCOUNT    0x00000020L
#define DDSD_LPSURFACE          0x00000800L
#define DDSD_PIXELFORMAT        0x00001000L

/* ═══════════════════════════════════════════════════════════ */
/* ── DDSURFACEDESC (DirectDraw 1.x) ─────────────────────────── */
/* ═══════════════════════════════════════════════════════════ */

/*
 * DDSURFACEDESC — 1.x surface descriptor.
 *
 * Layout matching DirectX 5 SDK DDSURFACEDESC:
 *   0x00  ddSize
 *   0x04  ddFlags
 *   0x08  ddCaps
 *   0x0C  ddX
 *   0x10  ddY
 *   0x14  union { lPitch / lWidth }
 *   0x18  dwBackBufferCount
 *   0x1C  union { wWidth / wHeight / lWidth / lHeight }
 *   0x20  union { lpSurface / lpDDSurfaceDesc }
 *
 * Total: 36 bytes (matches x86_32 ABI; pointer fields are uint32_t).
 */
typedef struct _DDSURFACEDESC {
    uint32_t  ddSize;               /* 0x00 size of this struct */
    uint32_t  ddFlags;              /* 0x04 DDSCAPS flags (used as flags) */
    uint32_t  ddCaps;               /* 0x08 DDSCAPS flags */
    uint32_t  ddX;                  /* 0x0C x offset */
    uint32_t  ddY;                  /* 0x10 y offset */
    union {
        int32_t   lPitch;           /* 0x14 bytes per line */
        uint32_t  lWidth;           /* 0x14 width in pixels */
    };
    uint32_t  dwBackBufferCount;    /* 0x18 number of back buffers */
    union {
        uint32_t  wWidth;           /* 0x1C width (16-bit) */
        uint32_t  wHeight;          /* 0x1C height (16-bit) */
        uint32_t  lWidth2;          /* 0x1C width (alias of lWidth at 0x14) */
        uint32_t  lHeight;          /* 0x1C height */
    };
    union {
        uint32_t  lpSurface;        /* 0x20 surface pointer (32-bit ABI) */
        uint32_t  lpDDSurfaceDesc;  /* 0x20 pointer to another desc (32-bit ABI) */
    };
} __attribute__((packed)) DDSURFACEDESC;

/* ═══════════════════════════════════════════════════════════ */
/* ── DDSURFACEDESC2 (DirectDraw 2.0+) ──────────────────────── */
/* ═══════════════════════════════════════════════════════════ */

/*
 * DDSURFACEDESC2 — 2.0+ surface descriptor.
 *
 * Layout matching DirectX 5 SDK:
 *   0x00  ddSize
 *   0x04  ddFlags
 *   0x08  dwHeight
 *   0x0C  dwWidth
 *   0x10  union { lPitch / dwLinearSize }
 *   0x14  dwBackBufferCount
 *   0x18  dwMipMapLevels
 *   0x1C  dwRefreshRate
 *   0x20  ddCaps
 *   0x24  ddColorKey
 *   0x28  lpSurface
 *
 * Total: 44 bytes (matches x86_32 ABI; pointer field is uint32_t).
 */
typedef struct _DDSURFACEDESC2 {
    uint32_t  ddSize;               /* 0x00 size of this struct */
    uint32_t  ddFlags;              /* 0x04 flags */
    uint32_t  dwHeight;             /* 0x08 height */
    uint32_t  dwWidth;              /* 0x0C width */
    union {
        int32_t   lPitch;           /* 0x10 bytes per line */
        uint32_t  dwLinearSize;     /* 0x10 size of linear surface */
    };
    uint32_t  dwBackBufferCount;    /* 0x14 */
    uint32_t  dwMipMapLevels;       /* 0x18 */
    uint32_t  dwRefreshRate;        /* 0x1C */
    uint32_t  ddCaps;               /* 0x20 DDSCAPS flags */
    uint32_t  ddColorKey;           /* 0x24 dwColorKeyLow (color key) */
    uint32_t  lpSurface;            /* 0x28 surface pointer (32-bit ABI) */
} __attribute__((packed)) DDSURFACEDESC2;

/* ── DDSURFACEDESC_UNION (for union parameter declarations) ── */

/* Used where the ABI accepts either v1 or v2 surface descriptors */
typedef union {
    DDSURFACEDESC  ddsd;
    DDSURFACEDESC2 ddsd2;
} DDSURFACEDESC_UNION;

/* ═══════════════════════════════════════════════════════════ */
/* ── DDPIXELFORMAT ──────────────────────────────────────────── */
/* ═══════════════════════════════════════════════════════════ */

/* Pixel format type identifiers */
#define DDPF_RGB                0x00000010L
#define DDPF_RGBA               0x00000030L
#define DDPF_FOURCC             0x00000004L
#define DDPF_YUV                0x00000008L
#define DDPF_LUMINANCE          0x00000020L
#define DDPF_ALPHAPIXELS        0x00000001L
#define DDPF_ALPHA              0x00000002L
#define DDPF_BUMPDUDV           0x00000080L
#define DDPF_ALPHAPREMULT       0x00000040L

typedef struct _DDPIXELFORMAT {
    uint32_t  ddSize;               /* 0x00 size of this struct */
    uint32_t  ddFlags;              /* 0x04 DDPF_* flags */
    uint32_t  ddFourCC;             /* 0x08 FourCC code */
    uint32_t  ddRGBBitCount;        /* 0x0C total RGB bits */
    uint32_t  ddRBitMask;           /* 0x10 red mask */
    uint32_t  ddGBitMask;           /* 0x14 green mask */
    uint32_t  ddBBitMask;           /* 0x18 blue mask */
    uint32_t  ddRGBAlphaBitMask;    /* 0x1C alpha mask */
} DDPIXELFORMAT;

/* ═══════════════════════════════════════════════════════════ */
/* ── DDCAPS ─────────────────────────────────────────────────── */
/* ═══════════════════════════════════════════════════════════ */

/* Device capability flags */
#define DDCAPS_BLT                      0x00000001L
#define DDCAPS_BLTFOCCOLORKEY           0x00000002L
#define DDCAPS_BLTDEPTHZBUFFER          0x00000008L
#define DDCAPS_BLTHW                    0x00000020L
#define DDCAPS_BLTSRCOLORKEY            0x00000040L
#define DDCAPS_BLTSRCOVERLAY            0x00000080L
#define DDCAPS_BLTALPHA                 0x00000100L
#define DDCAPS_COLORKEY                 0x00000200L
#define DDCAPS_GDI                      0x00000800L
#define DDCAPS_OVERLAY                  0x00001000L
#define DDCAPS_OVERLAYSTRETCH           0x00002000L
#define DDCAPS_PALETTE                  0x00004000L
#define DDCAPS_PALETTE8                 0x00008000L
#define DDCAPS_PRIMARYGCL               0x00010000L
#define DDCAPS_VIDEOPORT                0x00020000L
#define DDCAPS_FLIP                     0x00080000L
#define DDCAPS_COMPLEX                  0x00100000L
#define DDCAPS_CAV                      0x00200000L
#define DDCAPS_FULLSCREENFLIP           0x00400000L
#define DDCAPS_ALPHA                    0x01000000L
#define DDCAPS_ZBUFFERS                 0x08000000L
#define DDCAPS_MIXEDMODE                0x10000000L
#define DDCAPS_NO24BPP                  0x20000000L
#define DDCAPS_AUTOOVERLAY              0x80000000L
#define DDCAPS_OVERLAYCOLORKEY          0x00000010L
#define DDCAPS_OVERLAYCOLORKEYLOW       0x00000004L
#define DDCAPS_OVERLAYCOLORKEYHIGH      0x00000002L
#define DDCAPS_FULLSCREEN               0x00040000L

/*
 * DDCAPS — DirectX 5 SDK capability descriptor.
 *
 * 32-bit layout:
 *   0x00  dwSize           (DWORD)
 *   0x04  dwCaps           (DWORD)
 *   0x08  dwCaps2          (DWORD)
 *   0x0C  dwCKeyCaps       (DWORD)
 *   0x10  dwFXCaps         (DWORD)
 *   0x14  dwPaletteEntries (DWORD)
 *   0x18  dwSBCaps         (DWORD)   GDI / StretchBlt caps
 *   0x1C  dwSVCCaps        (DWORD)   video-port / SVC caps
 *   0x20  dwNVSCaps        (DWORD)   Nv/3D caps
 *   0x24  dddsCaps         DDSURFACEDESC2 (44 bytes on 32-bit)
 *   0x50  dwReserved1      (DWORD)
 *   0x54  dwReserved2      (DWORD)
 *
 * Total: 88 bytes (matches x86_32 ABI; embedded DDSURFACEDESC2 pointer field is uint32_t).
 *        96 bytes on x86_64 in native Windows (DDSURFACEDESC2 pointer is 8 bytes, alignment padding).
 */
typedef struct _DDCAPS {
    uint32_t       dwSize;          /* 0x00 size of this struct */
    uint32_t       dwCaps;          /* 0x04 DDCAPS_* flags */
    uint32_t       dwCaps2;         /* 0x08 extended caps */
    uint32_t       dwCKeyCaps;      /* 0x0C color key caps */
    uint32_t       dwFXCaps;        /* 0x10 FX caps */
    uint32_t       dwPaletteEntries;/* 0x14 palette entry count */
    uint32_t       dwSBCaps;        /* 0x18 GDI / StretchBlt caps */
    uint32_t       dwSVCCaps;       /* 0x1C video-port / SVC caps */
    uint32_t       dwNVSCaps;       /* 0x20 Nv/3D caps */
    DDSURFACEDESC2 dddsCaps;        /* 0x24 surface descriptor for caps */
    uint32_t       dwReserved1;     /* reserved */
    uint32_t       dwReserved2;     /* reserved */
} __attribute__((packed)) DDCAPS;

/* ═══════════════════════════════════════════════════════════ */
/* ── DDSCL_* cooperative level flags ────────────────────────── */
/* ═══════════════════════════════════════════════════════════ */

#define DDSCL_NORMAL          0x00000000L
#define DDSCL_EXCLUSIVE       0x00000001L
#define DDSCL_FULLSCREEN      0x00000002L
#define DDSCL_ALLOWREBOOT     0x00000004L
#define DDSCL_ALLOWMODEX      0x00000008L
#define DDSCL_CREATEDEVICEWINDOW 0x00000010L
#define DDSCL_MULTITHREADED   0x00000020L
#define DDSCL_FBRUSH          0x00000040L

/* ═══════════════════════════════════════════════════════════ */
/* ── DDBLT_* flags ──────────────────────────────────────────── */
/* ═══════════════════════════════════════════════════════════ */

#define DDBLT_SRCCOLORKEY         0x00000001L
#define DDBLT_KEYSRCOVERRIDE      0x00000002L
#define DDBLT_KEYDESTOVERRIDE     0x00000004L
#define DDBLT_ROP                 0x00000010L
#define DDBLT_DDFX                0x00000020L
#define DDBLT_AAFILL              0x00000040L
#define DDBLT_COLORFILL           0x00000400L
#define DDBLT_SRCOVERLAY          0x00000080L
#define DDBLT_DEPTHFILL           0x00000100L
#define DDBLT_WAIT                0x00000200L
#define DDBLT_ASYNC               0x00000800L
#define DDBLT_ZBUFFER             0x00000800L
#define DDBLT_ZBUFFEROVERRIDE     0x00001000L
#define DDBLT_NOCLIP              0x00080000L
#define DDBLT_DONOTWAIT           0x00000000L

/* ═══════════════════════════════════════════════════════════ */
/* ── DDBLTFAST_* flags ──────────────────────────────────────── */
/* ═══════════════════════════════════════════════════════════ */

#define DDBLTFAST_NOCOLORKEY      0x00000000L
#define DDBLTFAST_SRCCOLORKEY     0x00000001L
#define DDBLTFAST_DESTCOLORKEY    0x00000002L
#define DDBLTFAST_WAIT            0x00000004L
#define DDBLTFAST_NOCLEAR         0x00000008L
#define DDBLTFAST_CENTERED        0x00000010L
#define DDBLTFAST_SMOOTHALPHA     0x00000020L
#define DDBLTFAST_NOTRANSFORM     0x00000040L

/* ═══════════════════════════════════════════════════════════ */
/* ── DDBLTFX flags (used in DDBLTFX structure) ─────────────── */
/* ═══════════════════════════════════════════════════════════ */

#define DDBLTfx_DDFILL            0x00000001L
#define DDBLTfx_DEPTHFILL         0x00000002L

/* ═══════════════════════════════════════════════════════════ */
/* ── DDSetColorKey / DDGetBltStatus flags ───────────────────── */
/* ═══════════════════════════════════════════════════════════ */

#define DDSCKEY_DESTOVERLAY       0x00000200L
#define DDSCKEY_AUTOBLTFOCCOLORKEY 0x00000020L
#define DDSCKEY_AUTOBLTTEX        0x00000010L
#define DDSCKEY_AUTOBLT           0x00000008L
#define DDSCKEY_NOTFILL           0x00000004L
#define DDSCKEY_DDFILL            0x00000002L
#define DDSCKEY_AUTOPRIMARY       0x00000001L

/* ═══════════════════════════════════════════════════════════ */
/* ── DDPCAPS_* palette flags ──────────────────────────────── */
/* ═══════════════════════════════════════════════════════════ */

#define DDPCAPS_4BIT          0x00000001L
#define DDPCAPS_8BITENTRIES   0x00000002L
#define DDPCAPS_8BIT          0x00000004L
#define DDPCAPS_INITIALIZE    0x00000008L
#define DDPCAPS_PRIMARYSURFACE      0x00000010L
#define DDPCAPS_PRIMARYSURFACELEFT  0x00000020L
#define DDPCAPS_ALLOW256      0x00000040L
#define DDPCAPS_VSYNC         0x00000080L
#define DDPCAPS_1BIT          0x00000100L
#define DDPCAPS_2BIT          0x00000200L
#define DDPCAPS_ALPHA         0x00000400L

#define DDGBLTST_QUEUED           0x00000001L
#define DDGBLTST_INPROGRESS       0x00000002L
#define DDGBLTST_NEWDATA          0x00000004L
#define DDGBLTST_CMDRETRIEVED     0x00000008L

/* ═══════════════════════════════════════════════════════════ */
/* ── DDBLTFX (BltFX structure) ─────────────────────────────── */
/* ═══════════════════════════════════════════════════════════ */

typedef struct _DDBLTFX {
    uint32_t  dwSize;               /* 0x00 size of this struct */
    uint32_t  dwDDFX;               /* 0x04 DDBLTfx_* flags */
    uint32_t  dwROP;                /* 0x08 ROP code */
    uint32_t  pDDSrcColorKey;       /* 0x0C source color key (32-bit ABI) */
    uint32_t  pDDDestColorKey;      /* 0x10 dest color key (32-bit ABI) */
    uint32_t  pDDDestOverColorKey;  /* 0x14 overlay dest color key (32-bit ABI) */
    uint32_t  dwZBufferOp;          /* 0x18 z-buffer op */
    uint32_t  dwZBufferLow;         /* 0x1C z-buffer low */
    uint32_t  dwZBufferHigh;        /* 0x20 z-buffer high */
    uint32_t  dwZBufferBaseDest;    /* 0x24 z-buffer base dest */
    uint32_t  dwZBufferBaseDestMask; /* 0x28 z-buffer base dest mask */
    uint32_t  dwZBufferBaseDestConst; /* 0x2C z-buffer base dest const */
    uint32_t  pDDSrcRGB;            /* 0x30 source RGB fill color (32-bit ABI) */
    uint32_t  pDDDestRGB;           /* 0x34 dest RGB fill color (32-bit ABI) */
    uint32_t  pDDSrcDepthRGB;       /* 0x38 source depth RGB (32-bit ABI) */
    uint32_t  pDDDestDepthRGB;      /* 0x3C dest depth RGB (32-bit ABI) */
} __attribute__((packed)) DDBLTFX;

/* ═══════════════════════════════════════════════════════════ */
/* ── DDCOLORKEY ─────────────────────────────────────────────── */
/* ═══════════════════════════════════════════════════════════ */

typedef struct _DDCOLORKEY {
    uint32_t  dwColorSpaceLow;      /* 0x00 low color key value */
    uint32_t  dwColorSpaceHigh;     /* 0x04 high color key value */
} DDCOLORKEY;

typedef struct _DDPALETTEENTRY {
    uint8_t peRed;
    uint8_t peGreen;
    uint8_t peBlue;
    uint8_t peFlags;
} DDPALETTEENTRY;

/* ═══════════════════════════════════════════════════════════ */
/* ── DDRECT ─────────────────────────────────────────────────── */
/* ═══════════════════════════════════════════════════════════ */

typedef struct _DDRECT {
    uint32_t  left;                 /* 0x00 */
    uint32_t  top;                  /* 0x04 */
    uint32_t  right;                /* 0x08 */
    uint32_t  bottom;               /* 0x0C */
} DDRECT;

/* ═══════════════════════════════════════════════════════════ */
/* ── DirectDraw vtable typedefs ─────────────────────────────── */
/* ═══════════════════════════════════════════════════════════ */

/*
 * IDirectDrawVtbl — DirectDraw 1 layout returned by DirectDrawCreate().
 *
 * Offsets 0–2 are IUnknown, followed by the 20 IDirectDraw methods.
 * Later DirectDraw versions extend this layout after WaitForVerticalBlank.
 */
typedef struct _IDirectDrawVtbl {
    /* ── IUnknown (offsets 0–2) ────────────────────────── */
    HRESULT (KERNEL32_STUB *QueryInterface)(
        void *this_ptr,
        const GUID *riid,
        void **ppvObj);
    uint32_t (KERNEL32_STUB *AddRef)(void *this_ptr);
    uint32_t (KERNEL32_STUB *Release)(void *this_ptr);

    /* ── IDirectDraw methods (offsets 3–22) ───────────── */
    /*  3 */ HRESULT (KERNEL32_STUB *Compact)(void *this_ptr);
    /*  4 */ HRESULT (KERNEL32_STUB *CreateClipper)(
        void *this_ptr,
        uint32_t flags,
        void **lpClipper,
        void *unk);
    /*  5 */ HRESULT (KERNEL32_STUB *CreatePalette)(
        void *this_ptr,
        uint32_t flags,
        void *ddpalette,
        void **lpPalette,
        void *unk);
    /*  6 */ HRESULT (KERNEL32_STUB *CreateSurface)(
        void *this_ptr,
        void *ddsd,
        void **lpSurface,
        void *unk);
    /*  7 */ HRESULT (KERNEL32_STUB *DuplicateSurface)(
        void *this_ptr,
        void *lpDDSurface,
        void **lplpDupDDSurface);
    /*  8 */ HRESULT (KERNEL32_STUB *EnumDisplayModes)(
        void *this_ptr,
        uint32_t dwFlags,
        void *ddsd,
        void *lpContext,
        void *lpEnumCallback);
    /*  9 */ HRESULT (KERNEL32_STUB *EnumSurfaces)(
        void *this_ptr,
        uint32_t dwFlags,
        void *ddsd,
        void *lpContext,
        void *lpEnumCallback);
    /* 10 */ HRESULT (KERNEL32_STUB *FlipToGDISurface)(void *this_ptr);
    /* 11 */ HRESULT (KERNEL32_STUB *GetCaps)(
        void *this_ptr,
        void *ddcaps1,
        void *ddcaps2);
    /* 12 */ HRESULT (KERNEL32_STUB *GetDisplayMode)(void *this_ptr, void *ddsd);
    /* 13 */ HRESULT (KERNEL32_STUB *GetFourCCCodes)(
        void *this_ptr,
        uint32_t *lpNumCodes,
        uint32_t *lpCodes);
    /* 14 */ HRESULT (KERNEL32_STUB *GetGDISurface)(
        void *this_ptr,
        void **lpSurface);
    /* 15 */ HRESULT (KERNEL32_STUB *GetMonitorFrequency)(void *this_ptr, uint32_t *dwFreq);
    /* 16 */ HRESULT (KERNEL32_STUB *GetScanLine)(void *this_ptr, uint32_t *dwScanLine);
    /* 17 */ HRESULT (KERNEL32_STUB *GetVerticalBlankStatus)(
        void *this_ptr,
        int *lpInVerticalBlank);
    /* 18 */ HRESULT (KERNEL32_STUB *Initialize)(void *this_ptr, GUID *lpGUID);
    /* 19 */ HRESULT (KERNEL32_STUB *RestoreDisplayMode)(void *this_ptr);
    /* 20 */ HRESULT (KERNEL32_STUB *SetCooperativeLevel)(
        void *this_ptr, void *hwnd, uint32_t flags);
    /* 21 */ HRESULT (KERNEL32_STUB *SetDisplayMode)(
        void *this_ptr,
        uint32_t width,
        uint32_t height,
        uint32_t bpp);
    /* 22 */ HRESULT (KERNEL32_STUB *WaitForVerticalBlank)(
        void *this_ptr,
        uint32_t flags,
        void *hEvent);
} IDirectDrawVtbl;

/*
 * IDirectDrawSurfaceVtbl — 34 slots (DirectX 5 SDK).
 *   Offsets 0–2: IUnknown (QueryInterface, AddRef, Release)
 *   Offsets 3–33: 31 IDirectDrawSurface methods
 *
 * Notable: SetColorKey at offset 20; AddOverlayDirtyRect at offset 32.
 * This matches the real SDK vtable layout.
 */
typedef struct _IDirectDrawSurfaceVtbl {
    /* ── IUnknown (offsets 0–2) ────────────────────────── */
    /*  0 */ HRESULT (KERNEL32_STUB *QueryInterface)(
        void *this_ptr,
        const GUID *riid,
        void **ppvObj);
    /*  1 */ uint32_t (KERNEL32_STUB *AddRef)(void *this_ptr);
    /*  2 */ uint32_t (KERNEL32_STUB *Release)(void *this_ptr);

    /* ── IDirectDrawSurface methods (offsets 3–31) ────── */
    /*  3 */ HRESULT (KERNEL32_STUB *AddAttachedSurface)(
        void *this_ptr,
        void *lpDDS);
    /*  4 */ HRESULT (KERNEL32_STUB *AddOverlayDirtyRect)(
        void *this_ptr,
        void *lpDDRect);
    /*  5 */ HRESULT (KERNEL32_STUB *Blt)(
        void *this_ptr,
        void *lpDestRect,
        void *lpDDSrcSurface,
        void *lpSrcRect,
        uint32_t dwFlags,
        void *lpDDBltFX);
    /*  6 */ HRESULT (KERNEL32_STUB *BltBatch)(
        void *this_ptr,
        void *lpDDLBltData,
        uint32_t dwCount,
        uint32_t dwFlags);
    /*  7 */ HRESULT (KERNEL32_STUB *BltFast)(
        void *this_ptr,
        uint32_t dwX,
        uint32_t dwY,
        void *lpDDSrcSurface,
        void *lpSrcRect,
        uint32_t dwFlags);
    /*  8 */ HRESULT (KERNEL32_STUB *DeleteAttachedSurface)(
        void *this_ptr,
        uint32_t dwFlags,
        void *lpDDS);
    /*  9 */ HRESULT (KERNEL32_STUB *EnumAttachedSurfaces)(
        void *this_ptr,
        void *lpContext,
        void *lpEnumCallback);
    /* 10 */ HRESULT (KERNEL32_STUB *EnumOverlayZOrders)(
        void *this_ptr,
        uint32_t dwFlags,
        void *lpContext,
        void *lpEnumCallback);
    /* 11 */ HRESULT (KERNEL32_STUB *Flip)(
        void *this_ptr,
        void *lpDDSurface,
        uint32_t dwFlags);
    /* 12 */ HRESULT (KERNEL32_STUB *GetAttachedSurface)(
        void *this_ptr,
        void *lpDDSCaps,
        void **lppDDSSurface);
    /* 13 */ HRESULT (KERNEL32_STUB *GetBltStatus)(
        void *this_ptr,
        uint32_t dwFlags);
    /* 14 */ HRESULT (KERNEL32_STUB *GetCaps)(
        void *this_ptr,
        void *lpDDSCaps);
    /* 15 */ HRESULT (KERNEL32_STUB *GetClipper)(
        void *this_ptr,
        void **lppDDClipper);
    /* 16 */ HRESULT (KERNEL32_STUB *GetColorKey)(
        void *this_ptr,
        uint32_t dwFlags,
        void *lpDDColorKey);
    /* 17 */ HRESULT (KERNEL32_STUB *GetDC)(void *this_ptr, void **lphDC);
    /* 18 */ HRESULT (KERNEL32_STUB *GetFlipStatus)(
        void *this_ptr,
        uint32_t dwFlags);
    /* 19 */ HRESULT (KERNEL32_STUB *GetOverlayPosition)(
        void *this_ptr,
        int32_t *lpl,
        int32_t *lpt);
    /* 20 */ HRESULT (KERNEL32_STUB *GetPalette)(
        void *this_ptr,
        void **lppPalette);
    /* 21 */ HRESULT (KERNEL32_STUB *GetPixelFormat)(
        void *this_ptr,
        void *lpDDPixelFormat);
    /* 22 */ HRESULT (KERNEL32_STUB *GetSurfaceDesc)(
        void *this_ptr,
        void *lpDDSurfaceDesc);
    /* 23 */ HRESULT (KERNEL32_STUB *Initialize)(
        void *this_ptr,
        void *lpDDraw,
        void *lpDDSurfaceDesc);
    /* 24 */ HRESULT (KERNEL32_STUB *IsLost)(void *this_ptr);
    /* 25 */ HRESULT (KERNEL32_STUB *Lock)(
        void *this_ptr,
        void *lpDDRect,
        void *lpDDSurfaceDesc,
        uint32_t dwFlags,
        void *hEvent);
    /* 26 */ HRESULT (KERNEL32_STUB *ReleaseDC)(
        void *this_ptr,
        void *hDC);
    /* 27 */ HRESULT (KERNEL32_STUB *Restore)(void *this_ptr);
    /* 28 */ HRESULT (KERNEL32_STUB *SetClipper)(
        void *this_ptr,
        void *lpDDClipper);
    /* 29 */ HRESULT (KERNEL32_STUB *SetColorKey)(
        void *this_ptr,
        uint32_t dwFlags,
        void *lpDDColorKey);
    /* 30 */ HRESULT (KERNEL32_STUB *SetOverlayPosition)(
        void *this_ptr,
        int32_t l,
        int32_t t);
    /* 31 */ HRESULT (KERNEL32_STUB *SetPalette)(
        void *this_ptr,
        void *lpPalette);
    /* 32 */ HRESULT (KERNEL32_STUB *Unlock)(
        void *this_ptr,
        void *lpSurfaceData);
    /* 33 */ HRESULT (KERNEL32_STUB *UpdateOverlay)(
        void *this_ptr,
        void *lpSrcRect,
        void *lpDDSDstSurface,
        void *lpDstRect,
        uint32_t dwFlags,
        void *lpDDOverlayFx);
    /* 34 */ HRESULT (KERNEL32_STUB *UpdateOverlayDisplay)(
        void *this_ptr,
        uint32_t dwFlags);
    /* 35 */ HRESULT (KERNEL32_STUB *UpdateOverlayZOrder)(
        void *this_ptr,
        uint32_t dwFlags,
        void *lpDDSReferenceSurface);
} IDirectDrawSurfaceVtbl;

/*
 * IDirectDrawPaletteVtbl — DirectDraw 1 palette layout.
 *
 * Offsets 0–2 are IUnknown, followed by GetCaps, GetEntries,
 * Initialize, and SetEntries.
 */
typedef struct _IDirectDrawPaletteVtbl {
    /*  0 */ HRESULT (KERNEL32_STUB *QueryInterface)(
        void *this_ptr,
        const GUID *riid,
        void **ppvObj);
    /*  1 */ uint32_t (KERNEL32_STUB *AddRef)(void *this_ptr);
    /*  2 */ uint32_t (KERNEL32_STUB *Release)(void *this_ptr);
    /*  3 */ HRESULT (KERNEL32_STUB *GetCaps)(
        void *this_ptr,
        uint32_t *lpdwCaps);
    /*  4 */ HRESULT (KERNEL32_STUB *GetEntries)(
        void *this_ptr,
        void *ddpba,
        uint32_t dwStart,
        uint32_t dwCount,
        void *ddpe);
    /*  5 */ HRESULT (KERNEL32_STUB *Initialize)(
        void *this_ptr,
        void *lpDD,
        uint32_t dwFlags,
        void *lpDDColorTable);
    /*  6 */ HRESULT (KERNEL32_STUB *SetEntries)(
        void *this_ptr,
        void *ddpba,
        uint32_t dwStart,
        uint32_t dwCount,
        void *ddpe);
} IDirectDrawPaletteVtbl;

/*
 * IDirectDrawClipperVtbl — 8 methods.
 */
typedef struct _IDirectDrawClipperVtbl {
    /*  0 */ HRESULT (KERNEL32_STUB *QueryInterface)(
        void *this_ptr,
        const GUID *riid,
        void **ppvObj);
    /*  1 */ uint32_t (KERNEL32_STUB *AddRef)(void *this_ptr);
    /*  2 */ uint32_t (KERNEL32_STUB *Release)(void *this_ptr);
    /*  3 */ HRESULT (KERNEL32_STUB *SetHWnd)(
        void *this_ptr,
        uint32_t flags,
        void *hWnd);
    /*  4 */ HRESULT (KERNEL32_STUB *GetHWnd)(
        void *this_ptr,
        void **lphWnd);
    /*  5 */ HRESULT (KERNEL32_STUB *SetClipList)(
        void *this_ptr,
        void *lpClipList,
        void *hWnd);
    /*  6 */ HRESULT (KERNEL32_STUB *GetClipList)(
        void *this_ptr,
        void *lpClipList,
        void *hWnd);
    /*  7 */ HRESULT (KERNEL32_STUB *IsClipListChanged)(void *this_ptr);
} IDirectDrawClipperVtbl;

/* ═══════════════════════════════════════════════════════════ */
/* ── DirectDraw interface structs (COM interface layout) ────── */
/* ═══════════════════════════════════════════════════════════ */

/* IDirectDraw — the main DirectDraw interface */
typedef struct _IDirectDraw {
    IDirectDrawVtbl *lpVtbl;
} IDirectDraw;

/* IDirectDrawSurface — a DirectDraw surface */
typedef struct _IDirectDrawSurface {
    IDirectDrawSurfaceVtbl *lpVtbl;
} IDirectDrawSurface;

/* IDirectDrawPalette — a DirectDraw palette */
typedef struct _IDirectDrawPalette {
    IDirectDrawPaletteVtbl *lpVtbl;
} IDirectDrawPalette;

/* IDirectDrawClipper — a DirectDraw clipper */
typedef struct _IDirectDrawClipper {
    IDirectDrawClipperVtbl *lpVtbl;
} IDirectDrawClipper;

/* ═══════════════════════════════════════════════════════════ */
/* ── Pointer typedefs ──────────────────────────────────────── */
/* ═══════════════════════════════════════════════════════════ */

typedef IDirectDraw               *LPDIRECTDRAW;
typedef IDirectDrawSurface        *LPDIRECTDRAWSURFACE;
typedef IDirectDrawPalette        *LPDIRECTDRAWPALETTE;
typedef IDirectDrawClipper        *LPDIRECTDRAWCLIPPER;

typedef IDirectDrawVtbl           *LPDIRECTDRAWVTBL;
typedef IDirectDrawSurfaceVtbl    *LPDIRECTDRAWSURFACEVTBL;
typedef IDirectDrawPaletteVtbl    *LPDIRECTDRAWPALETTEVTBL;
typedef IDirectDrawClipperVtbl    *LPDIRECTDRAWCLIPPERVTBL;

/* ═══════════════════════════════════════════════════════════ */
/* ── Factory function ──────────────────────────────────────── */
/* ═══════════════════════════════════════════════════════════ */

HRESULT KERNEL32_STUB DirectDrawCreate(
    const GUID *Guid,
    LPDIRECTDRAW *lpDD,
    void *unk);

#endif /* MY_WINE_DDRAW_TYPES_H */
