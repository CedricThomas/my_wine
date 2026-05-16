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
    memset(&wc, 0, sizeof(wc));
    wc.lpszClassName = "HandleOwnershipTest";
    wc.lpfnWndProc = test_wndproc;

    T(RegisterClassA(&wc) != 0, "RegisterClassA failed");

    HWND hwnd = CreateWindowExA(0, wc.lpszClassName, "ownership",
                                WS_OVERLAPPEDWINDOW | WS_VISIBLE,
                                0, 0, 320, 200, 0, 0, 0, NULL);
    T(hwnd != 0, "CreateWindowExA failed");

    if (hwnd) {
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

        T(GetActiveWindow() == hwnd, "GetActiveWindow did not return created hwnd");
        T(GetFocus() == hwnd, "GetFocus did not return created hwnd");
        T(SetFocus(hwnd) == hwnd, "SetFocus did not return hwnd");
        T(GetFocus() == hwnd, "GetFocus changed after SetFocus");

        T(DestroyWindow(hwnd) == TRUE, "DestroyWindow failed");
        T(g_destroy_messages == 1, "WndProc did not receive exactly one WM_DESTROY");
        T(GetActiveWindow() == 0, "GetActiveWindow was not cleared on destroy");
        T(GetFocus() == 0, "GetFocus was not cleared on destroy");
    }

    if (g_failures == 0)
        printf("PASS: user32/backend handle ownership\n");
    else
        printf("FAIL: %d test(s) failed\n", g_failures);

    return g_failures;
}
