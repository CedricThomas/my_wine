#include <stdio.h>
#include <string.h>

#include "handle_manager.h"
#include "render_backend.h"
#include "src/msvcrt/user32_priv.h"
#include "user32_types.h"

KERNEL32_ABI ATOM RegisterClassA(const WNDCLASSA *lpWndClass);
KERNEL32_ABI HWND CreateWindowExA(DWORD dwExStyle, const char *lpClassName,
                                  const char *lpWindowName, DWORD dwStyle,
                                  int x, int y, int nWidth, int nHeight,
                                  HWND hWndParent, HMENU hMenu,
                                  HINSTANCE hInstance, void *lpParam);
KERNEL32_ABI BOOL DestroyWindow(HWND hwnd);
KERNEL32_ABI HWND GetActiveWindow(void);
KERNEL32_ABI HWND GetFocus(void);
KERNEL32_ABI HWND SetFocus(HWND hwnd);
KERNEL32_ABI LONG GetWindowLongA(HWND hwnd, int nIndex);
KERNEL32_ABI LONG SetWindowLongA(HWND hwnd, int nIndex, LONG dwNewLong);
KERNEL32_ABI LONG_PTR GetWindowLongPtrA(HWND hwnd, int nIndex);
KERNEL32_ABI LONG_PTR SetWindowLongPtrA(HWND hwnd, int nIndex, LONG_PTR dwNewLong);
KERNEL32_ABI HDC BeginPaint(HWND hwnd, PAINTSTRUCT *lpPaint);
KERNEL32_ABI BOOL EndPaint(HWND hwnd, const PAINTSTRUCT *lpPaint);
KERNEL32_ABI HCURSOR LoadCursorA(HINSTANCE hInstance, const char *lpCursorName);

static int g_failures = 0;
static int g_destroy_messages = 0;

#define T(cond, msg)                                                           \
    do {                                                                       \
        if (!(cond)) {                                                         \
            fprintf(stderr, "FAIL: %s\n", msg);                                \
            g_failures++;                                                      \
        }                                                                      \
    } while (0)

static LRESULT KERNEL32_ABI test_wndproc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    (void)hwnd;
    (void)wParam;
    (void)lParam;
    if (msg == WM_DESTROY)
        g_destroy_messages++;
    return 0;
}

int main(void)
{
    WNDCLASSA wc;
    char dynamic_name[32];
    memset(&wc, 0, sizeof(wc));
    wc.lpszClassName = "HandleOwnershipTest";
    wc.lpfnWndProc = test_wndproc;

    T(RegisterClassA(&wc) != 0, "RegisterClassA failed");
    T(RegisterClassA(&wc) == 1, "duplicate RegisterClassA should return existing atom");

    for (int i = 0; i < 20; i++) {
        snprintf(dynamic_name, sizeof(dynamic_name), "DynamicClass%02d", i);
        wc.lpszClassName = dynamic_name;
        T(RegisterClassA(&wc) == (ATOM)(i + 2), "dynamic RegisterClassA failed");
        memset(dynamic_name, 'x', strlen(dynamic_name));
        dynamic_name[sizeof(dynamic_name) - 1] = '\0';
    }

    HWND hwnd = CreateWindowExA(WS_EX_APPWINDOW, "HandleOwnershipTest", "ownership",
                                WS_OVERLAPPEDWINDOW | WS_VISIBLE,
                                0, 0, 320, 200, (HWND)0x1111, (HMENU)0x2222, (HINSTANCE)0x3333, NULL);
    HWND hwnd2 = CreateWindowExA(0, "DynamicClass19", "ownership-2",
                                 WS_OVERLAPPEDWINDOW | WS_VISIBLE,
                                 10, 10, 320, 200, 0, 0, 0, NULL);
    T(hwnd != 0, "CreateWindowExA failed");
    T(hwnd2 != 0, "CreateWindowExA for copied dynamic class failed");

    if (hwnd && hwnd2) {
        T(wine_handle_get_type((uint32_t)hwnd) == HANDLE_TYPE_HWIN,
          "HWND did not use HANDLE_TYPE_HWIN");

        void *entry = wine_handle_get((uint32_t)hwnd);
        T(entry != NULL, "HWND lookup returned NULL");

        rb_window_t backend = ((wine_window_entry *)entry)->sdl_window;
        T(backend != 0, "window entry missing backend window");
        T(wine_handle_get_type((uint32_t)backend) == HANDLE_TYPE_RB_WINDOW,
          "backend window did not use HANDLE_TYPE_RB_WINDOW");
        T(wine_handle_get((uint32_t)backend) != entry,
          "backend window aliased wine_window_entry storage");

        T(GetActiveWindow() == hwnd2, "GetActiveWindow did not return latest hwnd");
        T(GetFocus() == hwnd2, "GetFocus did not return latest hwnd");
        T(SetFocus(hwnd) == hwnd2, "SetFocus should return previous focus window");
        T(GetFocus() == hwnd, "GetFocus did not change to hwnd");
        T(SetFocus(hwnd2) == hwnd, "SetFocus should return first window on transition");
        T(GetFocus() == hwnd2, "GetFocus did not change to hwnd2");

        T(GetWindowLongA(hwnd, GWL_STYLE) == (LONG)(WS_OVERLAPPEDWINDOW | WS_VISIBLE),
          "GetWindowLongA(GWL_STYLE) returned wrong style");
        T(GetWindowLongA(hwnd, GWL_EXSTYLE) == (LONG)WS_EX_APPWINDOW,
          "GetWindowLongA(GWL_EXSTYLE) returned wrong exstyle");
        T(GetWindowLongPtrA(hwnd, GWLP_HINSTANCE) == (LONG_PTR)(uintptr_t)0x3333,
          "GetWindowLongPtrA(GWLP_HINSTANCE) returned wrong instance");
        T(GetWindowLongPtrA(hwnd, GWLP_HWNDPARENT) == (LONG_PTR)(uintptr_t)0x1111,
          "GetWindowLongPtrA(GWLP_HWNDPARENT) returned wrong parent");
        T(GetWindowLongPtrA(hwnd, GWLP_ID) == (LONG_PTR)(uintptr_t)0x2222,
          "GetWindowLongPtrA(GWLP_ID) returned wrong menu/id");
        T(GetWindowLongPtrA(hwnd, GWLP_WNDPROC) == (LONG_PTR)(intptr_t)test_wndproc,
          "GetWindowLongPtrA(GWLP_WNDPROC) did not preserve wndproc pointer");
        T(SetWindowLongA(hwnd, GWL_EXSTYLE, WS_EX_TOOLWINDOW) == (LONG)WS_EX_APPWINDOW,
          "SetWindowLongA(GWL_EXSTYLE) returned wrong previous value");
        T(GetWindowLongA(hwnd, GWL_EXSTYLE) == (LONG)WS_EX_TOOLWINDOW,
          "SetWindowLongA(GWL_EXSTYLE) did not update exstyle");
        T(SetWindowLongPtrA(hwnd, GWLP_USERDATA, ((LONG_PTR)1 << 34) | 0x55AA) == 0,
          "SetWindowLongPtrA(GWLP_USERDATA) returned wrong previous value");
        T(GetWindowLongPtrA(hwnd, GWLP_USERDATA) == (((LONG_PTR)1 << 34) | 0x55AA),
          "GetWindowLongPtrA(GWLP_USERDATA) did not preserve pointer-width value");

        PAINTSTRUCT ps;
        memset(&ps, 0, sizeof(ps));
        HDC hdc = BeginPaint(hwnd, &ps);
        T(hdc != 0, "BeginPaint failed");
        T(ps.hdc == hdc, "BeginPaint did not store hdc in PAINTSTRUCT");
        T(wine_handle_get_type((uint32_t)hdc) == HANDLE_TYPE_DC,
          "BeginPaint did not allocate a DC handle");
        T(EndPaint(hwnd, &ps) == TRUE, "EndPaint failed");
        T(wine_handle_get((uint32_t)hdc) == NULL, "EndPaint did not free the DC handle");

        const char *cursor_ids[] = { IDC_ARROW, IDC_CROSS, IDC_IBEAM, IDC_WAIT, IDC_HAND };
        for (size_t i = 0; i < sizeof(cursor_ids) / sizeof(cursor_ids[0]); i++) {
            HCURSOR cursor = LoadCursorA(0, cursor_ids[i]);
            T(cursor != 0, "LoadCursorA returned NULL for a standard cursor");
            if (cursor) {
                rb_cursor_t backend_cursor =
                    (rb_cursor_t)(uintptr_t)wine_handle_get((uint32_t)cursor);
                T(backend_cursor != 0, "LoadCursorA did not allocate a backend cursor");
                if (backend_cursor)
                    T(rb_cursor_destroy(backend_cursor) == RB_OK,
                      "rb_cursor_destroy failed for LoadCursorA result");
                wine_handle_free((uint32_t)cursor);
            }
        }

        T(DestroyWindow(hwnd) == TRUE, "DestroyWindow failed");
        T(GetActiveWindow() == hwnd2, "GetActiveWindow did not fall back to the remaining window");
        T(GetFocus() == hwnd2, "GetFocus did not fall back to the remaining window");
        T(DestroyWindow(hwnd2) == TRUE, "DestroyWindow on second window failed");
        T(g_destroy_messages == 2, "WndProc did not receive exactly one WM_DESTROY per window");
        T(GetActiveWindow() == 0, "GetActiveWindow was not cleared on destroy");
        T(GetFocus() == 0, "GetFocus was not cleared on destroy");
    }

    if (g_failures == 0)
        printf("PASS: user32/backend handle ownership\n");
    else
        printf("FAIL: %d test(s) failed\n", g_failures);

    return g_failures;
}
