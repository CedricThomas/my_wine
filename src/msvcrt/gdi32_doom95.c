#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <stdlib.h>

#include "include/user32_types.h"
#include "include/handle_manager.h"

#define HORZRES 8
#define VERTRES 10
#define BITSPIXEL 12
#define NUMCOLORS 24

typedef struct {
    int32_t bmType;
    int32_t bmWidth;
    int32_t bmHeight;
    int32_t bmWidthBytes;
    uint16_t bmPlanes;
    uint16_t bmBitsPixel;
    void *bmBits;
} BITMAP_WINE;

typedef struct {
    BITMAP_WINE bmp;
} wine_bitmap;

typedef struct {
    uint32_t count;
} wine_palette;

KERNEL32_STUB
void *CreateDCA(const char *lpszDriver, const char *lpszDevice,
                const char *lpszOutput, const void *lpInitData)
{
    (void)lpszDriver;
    (void)lpszDevice;
    (void)lpszOutput;
    (void)lpInitData;
    return FORCE_PTR_RETURN((void *)(uintptr_t)wine_handle_alloc(HANDLE_TYPE_DC, NULL));
}

KERNEL32_STUB
void *CreateDIBitmap(void *hdc, const void *pbmih, uint32_t flInit, const void *pjBits,
                     const void *pbmi, uint32_t iUsage)
{
    wine_bitmap *bmp = malloc(sizeof(*bmp));
    (void)hdc;
    (void)pbmih;
    (void)flInit;
    (void)pjBits;
    (void)pbmi;
    (void)iUsage;

    if (!bmp)
        return FORCE_PTR_RETURN(NULL);
    memset(bmp, 0, sizeof(*bmp));
    return FORCE_PTR_RETURN((void *)(uintptr_t)wine_handle_alloc(HANDLE_TYPE_BITMAP, bmp));
}

KERNEL32_STUB
void *CreateFontA(int cHeight, int cWidth, int cEscapement, int cOrientation, int cWeight,
                  uint32_t bItalic, uint32_t bUnderline, uint32_t bStrikeOut,
                  uint32_t iCharSet, uint32_t iOutPrecision, uint32_t iClipPrecision,
                  uint32_t iQuality, uint32_t iPitchAndFamily, const char *pszFaceName)
{
    (void)cHeight;
    (void)cWidth;
    (void)cEscapement;
    (void)cOrientation;
    (void)cWeight;
    (void)bItalic;
    (void)bUnderline;
    (void)bStrikeOut;
    (void)iCharSet;
    (void)iOutPrecision;
    (void)iClipPrecision;
    (void)iQuality;
    (void)iPitchAndFamily;
    (void)pszFaceName;
    return FORCE_PTR_RETURN((void *)(uintptr_t)wine_handle_alloc(HANDLE_TYPE_HFONT, NULL));
}

KERNEL32_STUB
void *CreatePalette(const void *lplgpl)
{
    wine_palette *pal = malloc(sizeof(*pal));
    (void)lplgpl;
    if (!pal)
        return FORCE_PTR_RETURN(NULL);
    pal->count = 256;
    return FORCE_PTR_RETURN((void *)(uintptr_t)wine_handle_alloc(HANDLE_TYPE_PALETTE, pal));
}

KERNEL32_STUB
int DeleteDC(void *hdc)
{
    uint32_t handle = (uint32_t)(uintptr_t)hdc;
    if (wine_handle_get_type(handle) == HANDLE_TYPE_DC)
        wine_handle_free(handle);
    return 1;
}

KERNEL32_STUB
int DeleteObject(void *hObject)
{
    uint32_t handle = (uint32_t)(uintptr_t)hObject;
    uint8_t type = wine_handle_get_type(handle);

    if (type == HANDLE_TYPE_BITMAP || type == HANDLE_TYPE_PALETTE || type == HANDLE_TYPE_HFONT) {
        free(wine_handle_get(handle));
        wine_handle_free(handle);
    }
    return 1;
}

KERNEL32_STUB
int GetDeviceCaps(void *hdc, int index)
{
    (void)hdc;
    switch (index) {
    case HORZRES: return 640;
    case VERTRES: return 480;
    case BITSPIXEL: return 32;
    case NUMCOLORS: return 256;
    default: return 0;
    }
}

KERNEL32_STUB
int GetObjectA(void *hgdiobj, int cbBuffer, void *lpvObject)
{
    uint32_t handle = (uint32_t)(uintptr_t)hgdiobj;
    wine_bitmap *bmp;

    if (wine_handle_get_type(handle) != HANDLE_TYPE_BITMAP || !lpvObject || cbBuffer <= 0)
        return 0;
    bmp = (wine_bitmap *)wine_handle_get(handle);
    if (!bmp)
        return 0;
    if ((size_t)cbBuffer > sizeof(bmp->bmp))
        cbBuffer = (int)sizeof(bmp->bmp);
    memcpy(lpvObject, &bmp->bmp, (size_t)cbBuffer);
    return cbBuffer;
}

KERNEL32_STUB
void *GetStockObject(int fnObject)
{
    return FORCE_PTR_RETURN((void *)(uintptr_t)(0x20000000u + (uint32_t)fnObject));
}

KERNEL32_STUB
uint32_t GetSystemPaletteEntries(void *hdc, uint32_t iStartIndex, uint32_t nEntries, void *lppe)
{
    uint8_t *dst = (uint8_t *)lppe;
    uint32_t i;
    (void)hdc;

    if (!dst)
        return 0;
    for (i = 0; i < nEntries; i++) {
        uint8_t v = (uint8_t)((iStartIndex + i) & 0xffu);
        dst[i * 4 + 0] = v;
        dst[i * 4 + 1] = v;
        dst[i * 4 + 2] = v;
        dst[i * 4 + 3] = 0;
    }
    return nEntries;
}

KERNEL32_STUB
uint32_t RealizePalette(void *hdc)
{
    (void)hdc;
    return 1;
}

KERNEL32_STUB
void *SelectPalette(void *hdc, void *hpal, int bForceBackground)
{
    (void)hdc;
    (void)bForceBackground;
    return FORCE_PTR_RETURN(hpal);
}

KERNEL32_STUB
uint32_t SetBkColor(void *hdc, uint32_t color)
{
    (void)hdc;
    return color;
}

KERNEL32_STUB
uint32_t SetTextColor(void *hdc, uint32_t color)
{
    (void)hdc;
    return color;
}

KERNEL32_STUB
int StretchDIBits(void *hdc, int xDest, int yDest, int DestWidth, int DestHeight,
                  int xSrc, int ySrc, int SrcWidth, int SrcHeight, const void *lpBits,
                  const void *lpBitsInfo, uint32_t iUsage, uint32_t rop)
{
    (void)hdc;
    (void)xDest;
    (void)yDest;
    (void)DestWidth;
    (void)DestHeight;
    (void)xSrc;
    (void)ySrc;
    (void)SrcWidth;
    (void)lpBits;
    (void)lpBitsInfo;
    (void)iUsage;
    (void)rop;
    return SrcHeight;
}

KERNEL32_STUB
int UnrealizeObject(void *h)
{
    (void)h;
    return 1;
}
