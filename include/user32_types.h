/*
 * user32_types.h — Windows user32.dll types, structs, and constants.
 *
 * Self-contained header for the my_wine user32 stub layer.
 * No dependency on any real windows.h.
 */

#ifndef MY_WINE_USER32_TYPES_H
#define MY_WINE_USER32_TYPES_H

#include <stdint.h>
#include "wine_abi.h"

/* ---- Basic Windows types ---- */

typedef int32_t     BOOL;
typedef uint8_t     BYTE;
typedef uint16_t    WORD;
typedef uint32_t    DWORD;
typedef uint32_t    UINT;
typedef int32_t     INT;
typedef int32_t     LONG;
typedef uint32_t    ULONG;
typedef uintptr_t   ULONG_PTR;

typedef intptr_t    LRESULT;
typedef uintptr_t   WPARAM;
typedef intptr_t    LPARAM;
typedef int16_t     SHORT;

#define TRUE  1
#define FALSE 0

/* ---- Handle types (opaque pointer-sized IDs) ---- */

typedef uintptr_t HWND;
typedef uintptr_t HDC;
typedef uintptr_t HCURSOR;
typedef uintptr_t HICON;
typedef uintptr_t HINSTANCE;
typedef uintptr_t HBRUSH;
typedef uintptr_t HMENU;
typedef uintptr_t HMONITOR;
typedef uintptr_t HDESK;
typedef uintptr_t HHOOK;
typedef uintptr_t ATOM;

/* ---- Point / Rect ---- */

typedef struct {
    LONG x;
    LONG y;
} POINT;

typedef struct {
    LONG left;
    LONG top;
    LONG right;
    LONG bottom;
} RECT;

/* ---- Message ---- */

typedef struct {
    HWND    hwnd;
    UINT    message;
    WPARAM  wParam;
    LPARAM  lParam;
    UINT    time;
    POINT   pt;
} MSG;

/* ---- Window Procedure ---- */

typedef LRESULT (KERNEL32_ABI *WNDPROC)(HWND, UINT, WPARAM, LPARAM);

/* ---- Window Class ---- */

typedef struct {
    UINT       style;
    WNDPROC    lpfnWndProc;
    int32_t    cbClsExtra;
    int32_t    cbWndExtra;
    HINSTANCE  hInstance;
    HICON      hIcon;
    HCURSOR    hCursor;
    HBRUSH     hbrBackground;
    const char *lpszMenuName;
    const char *lpszClassName;
} WNDCLASSA;

typedef struct {
    void       *lpCreateParams;
    HINSTANCE   hInstance;
    HMENU       hMenu;
    HWND        hwndParent;
    int         cy;
    int         cx;
    int         y;
    int         x;
    LONG        style;
    const char *lpszName;
    const char *lpszClass;
    DWORD       dwExStyle;
} CREATESTRUCTA;

/* ---- Paint Struct ---- */

typedef struct {
    HDC     hdc;
    BOOL    fErase;
    RECT    rcPaint;
    BOOL    fRestore;
    BOOL    fPaintValidateRect;
} PAINTSTRUCT;

/* ---- MINMAXINFO (for WM_GETMINMAXINFO) ---- */

typedef struct {
    POINT ptReserved;
    POINT ptMaxSize;
    POINT ptMaxPosition;
    POINT ptMinTrackSize;
    POINT ptMaxTrackSize;
} MINMAXINFO;

/*
 * ── Windows Message Constants (WM_*) ──────────────────────────
 */

#define WM_CREATE              0x0001
#define WM_NCCREATE            0x0081
#define WM_DESTROY             0x0002
#define WM_MOVE                0x0003
#define WM_SIZE                0x0005
#define WM_ACTIVATE            0x0006
#define WM_SETFOCUS            0x0007
#define WM_KILLFOCUS           0x0008
#define WM_CLOSE               0x0010
#define WM_PAINT               0x000F
#define WM_GETTEXT             0x000D
#define WM_GETTEXTLENGTH       0x000E
#define WM_SETTEXT             0x000C
#define WM_SETCURSOR           0x0020
#define WM_GETMINMAXINFO       0x0024
#define WM_QUIT                0x0012
#define WM_DISPLAYCHANGE       0x007E
#define WM_KEYDOWN             0x0100
#define WM_KEYUP               0x0101
#define WM_CHAR                0x0102
#define WM_SYSKEYDOWN          0x0104
#define WM_SYSKEYUP            0x0105
#define WM_SYSCOMMAND          0x0112
#define WM_MOUSEMOVE           0x0200
#define WM_LBUTTONDOWN         0x0201
#define WM_LBUTTONUP           0x0202
#define WM_LBUTTONDBLCLK       0x0203
#define WM_RBUTTONDOWN         0x0204
#define WM_RBUTTONUP           0x0205
#define WM_RBUTTONDBLCLK       0x0206
#define WM_MBUTTONDOWN         0x0207
#define WM_MBUTTONUP           0x0208
#define WM_MBUTTONDBLCLK       0x0209
#define WM_MOUSEWHEEL          0x020A

/* ── WM_ACTIVATE sub-codes (wParam >> 16) ─────────────────── */

#define WA_INACTIVE    0
#define WA_ACTIVE      1
#define WA_CLICKACTIVE 2

/*
 * ── Cursor IDs (IDC_*) ───────────────────────────────────────
 *
 * Per real windows.h: integer cursor IDs cast to pointer type.
 * LoadCursorA receives these as LPCTSTR and recovers the ID via (uintptr_t)lpCursorName.
 */

#define IDC_ARROW       ((const char *)(uintptr_t)32512)
#define IDC_IBEAM       ((const char *)(uintptr_t)32513)
#define IDC_WAIT        ((const char *)(uintptr_t)32514)
#define IDC_CROSS       ((const char *)(uintptr_t)32515)
#define IDC_UPARROW     ((const char *)(uintptr_t)32516)
#define IDC_SIZE        ((const char *)(uintptr_t)32640)
#define IDC_ICON        ((const char *)(uintptr_t)32641)
#define IDC_SIZENWSE    ((const char *)(uintptr_t)32642)
#define IDC_SIZENS      ((const char *)(uintptr_t)32643)
#define IDC_SIZENESW    ((const char *)(uintptr_t)32644)
#define IDC_SIZEWE      ((const char *)(uintptr_t)32645)
#define IDC_SIZEALL     ((const char *)(uintptr_t)32646)
#define IDC_HAND        ((const char *)(uintptr_t)32649)
#define IDC_NO          ((const char *)(uintptr_t)32648)

/*
 * ── Show Window Constants (SW_*) ─────────────────────────────
 */

#define SW_HIDE            0
#define SW_SHOWNORMAL      1
#define SW_SHOWMINIMIZED   2
#define SW_SHOWMAXIMIZED   3
#define SW_SHOW            5
#define SW_MINIMIZE        6
#define SW_SHOWNA          8
#define SW_RESTORE         9
#define SW_SHOWDEFAULT     10

/*
 * ── Window Styles (WS_*) ─────────────────────────────────────
 */

#define WS_OVERLAPPED      0x00000000
#define WS_CAPTION         0x00C00000
#define WS_BORDER          0x00800000
#define WS_SYSMENU         0x00080000
#define WS_THICKFRAME      0x00040000
#define WS_MINIMIZEBOX     0x00010000
#define WS_MAXIMIZEBOX     0x00020000
#define WS_VISIBLE         0x10000000
#define WS_DISABLED        0x08000000
#define WS_CLIPSIBLINGS    0x04000000
#define WS_CLIPCHILDREN    0x02000000
#define WS_HSCROLL         0x00100000
#define WS_VSCROLL         0x00200000
#define WS_GROUP           0x00020000
#define WS_TABSTOP         0x00010000
#define WS_CHILD           0x40000000
#define WS_POPUP           0x80000000

#define WS_OVERLAPPEDWINDOW (WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU \
                            | WS_THICKFRAME | WS_MINIMIZEBOX | WS_MAXIMIZEBOX)

#define WS_TILEDWINDOW     WS_OVERLAPPEDWINDOW

/* Extended window styles */
#define WS_EX_TOOLWINDOW    0x00000080
#define WS_EX_APPWINDOW     0x00040000
#define WS_EX_WINDOWEDGE    0x00000100
#define WS_EX_TRANSPARENT   0x00000020

/*
 * ── GetWindowLongA / SetWindowLongA indices (GWL_*) ──────────
 */

#define GWL_WNDPROC    (-4)
#define GWL_HINSTANCE  (-6)
#define GWL_HWNDPARENT (-8)
#define GWL_STYLE      (-16)
#define GWL_EXSTYLE    (-20)
#define GWL_USERDATA   (-21)
#define GWL_ID         (-12)

/*
 * ── System Command IDs (SC_*) ────────────────────────────────
 */

#define SC_SIZE         0xF000
#define SC_MOVE         0xF010
#define SC_MINIMIZE     0xF020
#define SC_MAXIMIZE     0xF030
#define SC_NEXTWINDOW   0xF040
#define SC_PREVWINDOW   0xF050
#define SC_CLOSE        0xF060
#define SC_VSCROLL      0xF070
#define SC_HSCROLL      0xF080
#define SC_RESTORE      0xF120
#define SC_TASKLIST     0xF160
#define SC_SCREENSAVE   0xF140
#define SC_HOTKEY       0xF150

/*
 * ── Virtual Key Codes (VK_*) ─────────────────────────────────
 */

#define VK_LBUTTON       0x01
#define VK_RBUTTON       0x02
#define VK_MBUTTON       0x04
#define VK_BACK          0x08
#define VK_TAB           0x09
#define VK_RETURN        0x0D
#define VK_SHIFT         0x10
#define VK_CONTROL       0x11
#define VK_MENU          0x12
#define VK_PAUSE         0x13
#define VK_CAPITAL       0x14
#define VK_ESCAPE        0x1B
#define VK_SPACE         0x20
#define VK_PRIOR         0x21  /* Page Up */
#define VK_NEXT          0x22  /* Page Down */
#define VK_END           0x23
#define VK_HOME          0x24
#define VK_LEFT          0x25
#define VK_UP            0x26
#define VK_RIGHT         0x27
#define VK_DOWN          0x28
#define VK_INSERT        0x2D
#define VK_DELETE        0x2E
#define VK_LWIN          0x5B
#define VK_RWIN          0x5C
#define VK_APPS          0x5D
#define VK_NUMPAD0       0x60
#define VK_NUMPAD1       0x61
#define VK_NUMPAD2       0x62
#define VK_NUMPAD3       0x63
#define VK_NUMPAD4       0x64
#define VK_NUMPAD5       0x65
#define VK_NUMPAD6       0x66
#define VK_NUMPAD7       0x67
#define VK_NUMPAD8       0x68
#define VK_NUMPAD9       0x69
#define VK_F1            0x70
#define VK_F2            0x71
#define VK_F3            0x72
#define VK_F4            0x73
#define VK_F5            0x74
#define VK_F6            0x75
#define VK_F7            0x76
#define VK_F8            0x77
#define VK_F9            0x78
#define VK_F10           0x79
#define VK_F11           0x7A
#define VK_F12           0x7B
#define VK_LSHIFT        0xA0
#define VK_RSHIFT        0xA1
#define VK_LCONTROL      0xA2
#define VK_RCONTROL      0xA3
#define VK_LMENU         0xA4
#define VK_RMENU         0xA5
#define VK_SCROLL        0x91
#define VK_NUMLOCK       0x90
#define VK_SNAPSHOT      0x2C

/*
 * ── System Metrics (SM_*) ─────────────────────────────────────
 */

#define SM_CXSCREEN      0
#define SM_CYSCREEN      1
#define SM_CXBORDER      2
#define SM_CYBORDER      3
#define SM_CXFULLSCREEN  16
#define SM_CYFULLSCREEN  17
#define SM_CXMENUSIZE    54
#define SM_CYMENUSIZE    55

/*
 * ── Background Colors (stock brushes) ────────────────────────
 */

#define COLOR_WINDOW       5
#define COLOR_WINDOWTEXT   18

/* Background brush constants (used as HBRUSH values) */
#define WHITE_BRUSH        0
#define LTGRAY_BRUSH       1
#define GRAY_BRUSH         2
#define DKGRAY_BRUSH       3
#define BLACK_BRUSH        4
#define NULL_BRUSH         5

/*
 * ── Pen Styles ────────────────────────────────────────────────
 */

#define PS_SOLID           0
#define PS_DASH            1
#define PS_DOT             2
#define PS_DASHDOT         3
#define PS_NULL            5

/*
 * ── Hit Test Codes (HT_*) ─────────────────────────────────────
 */

#define HTERROR            (-1)
#define HTTRANSPARENT      0
#define HTNOWHERE          1
#define HTCLIENT           1
#define HTCAPTION          2
#define HTSYSMENU          3
#define HTGROWBOX          4
#define HTMINBUTTON        8
#define HTMAXBUTTON        9
#define HTCLOSE           20

#endif /* MY_WINE_USER32_TYPES_H */
