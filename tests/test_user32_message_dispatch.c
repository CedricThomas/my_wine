#include <stdio.h>
#include <string.h>

#include "user32_types.h"

KERNEL32_ABI ATOM RegisterClassA(const WNDCLASSA *lpWndClass);
KERNEL32_ABI HWND CreateWindowExA(DWORD dwExStyle, const char *lpClassName,
                                  const char *lpWindowName, DWORD dwStyle,
                                  int x, int y, int nWidth, int nHeight,
                                  HWND hWndParent, HMENU hMenu,
                                  HINSTANCE hInstance, void *lpParam);
KERNEL32_ABI BOOL DestroyWindow(HWND hwnd);
KERNEL32_ABI LRESULT DispatchMessageA(const MSG *lpMsg);
KERNEL32_ABI LRESULT SendMessageA(HWND hWnd, UINT Msg, WPARAM wParam, LPARAM lParam);
KERNEL32_ABI LRESULT CallWindowProcA(WNDPROC lpPrevWndFunc, HWND hWnd,
                                     UINT Msg, WPARAM wParam, LPARAM lParam);
KERNEL32_ABI LRESULT DefWindowProcA(HWND hWnd, UINT Msg, WPARAM wParam, LPARAM lParam);
KERNEL32_ABI HWND GetActiveWindow(void);

static int g_failures = 0;
static int g_close_messages = 0;
static int g_destroy_messages = 0;
static int g_send_messages = 0;
static int g_callproc_messages = 0;

#define T(cond, msg)                                                           \
    do {                                                                       \
        if (!(cond)) {                                                         \
            fprintf(stderr, "FAIL: %s\n", msg);                                \
            g_failures++;                                                      \
        }                                                                      \
    } while (0)

#define TEST_SEND_MESSAGE     0x0401
#define TEST_CALLPROC_MESSAGE 0x0402
#define TEST_SEND_RESULT      0x1234
#define TEST_CALLPROC_RESULT  0x5678

static LRESULT KERNEL32_ABI test_wndproc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg) {
    case WM_CLOSE:
        g_close_messages++;
        return DefWindowProcA(hwnd, msg, wParam, lParam);
    case WM_DESTROY:
        g_destroy_messages++;
        return 0;
    case TEST_SEND_MESSAGE:
        g_send_messages++;
        return TEST_SEND_RESULT;
    case TEST_CALLPROC_MESSAGE:
        g_callproc_messages++;
        return TEST_CALLPROC_RESULT;
    default:
        return 0;
    }
}

int main(void)
{
    WNDCLASSA wc;
    memset(&wc, 0, sizeof(wc));
    wc.lpszClassName = "MessageDispatchTest";
    wc.lpfnWndProc = test_wndproc;

    T(RegisterClassA(&wc) != 0, "RegisterClassA failed");

    HWND hwnd = CreateWindowExA(0, wc.lpszClassName, "dispatch",
                                WS_OVERLAPPEDWINDOW | WS_VISIBLE,
                                0, 0, 320, 200, 0, 0, 0, NULL);
    T(hwnd != 0, "CreateWindowExA failed");

    if (hwnd) {
        T(SendMessageA(hwnd, TEST_SEND_MESSAGE, 0, 0) == TEST_SEND_RESULT,
          "SendMessageA did not return wndproc result");
        T(g_send_messages == 1, "SendMessageA did not reach wndproc");

        T(CallWindowProcA(test_wndproc, hwnd, TEST_CALLPROC_MESSAGE, 0, 0) == TEST_CALLPROC_RESULT,
          "CallWindowProcA did not return wndproc result");
        T(g_callproc_messages == 1, "CallWindowProcA did not reach wndproc");

        MSG msg;
        memset(&msg, 0, sizeof(msg));
        msg.hwnd = hwnd;
        msg.message = WM_CLOSE;

        T(DispatchMessageA(&msg) == 0, "DispatchMessageA did not return WM_CLOSE result");
        T(g_close_messages == 1, "DispatchMessageA did not reach wndproc for WM_CLOSE");
        T(g_destroy_messages == 1, "WM_CLOSE default path did not send exactly one WM_DESTROY");
        T(GetActiveWindow() == 0, "WM_CLOSE default path did not destroy the window");

        T(DestroyWindow(hwnd) == FALSE, "DestroyWindow should fail after WM_CLOSE destroyed the hwnd");
    }

    if (g_failures == 0)
        printf("PASS: user32 message dispatch\n");
    else
        printf("FAIL: %d test(s) failed\n", g_failures);

    return g_failures;
}
