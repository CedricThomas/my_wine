#include <windows.h>

#include "../harness.h"

static HWND g_primary = NULL;
static HWND g_secondary = NULL;

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg) {
    case WM_CLOSE:
        if (hwnd == g_primary && g_secondary != NULL) {
            DestroyWindow(g_secondary);
            return DefWindowProcA(hwnd, msg, wParam, lParam);
        }

        if (hwnd == g_secondary) {
            return DefWindowProcA(hwnd, msg, wParam, lParam);
        }

        return DefWindowProcA(hwnd, msg, wParam, lParam);

    case WM_DESTROY:
        if (hwnd == g_secondary) {
            g_secondary = NULL;
            return 0;
        }

        if (hwnd == g_primary) {
            g_primary = NULL;
            PostQuitMessage(0);
            return 0;
        }
        return 0;

    default:
        return DefWindowProcA(hwnd, msg, wParam, lParam);
    }
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow)
{
    WNDCLASSA wc = {0};

    (void)hPrevInstance;
    (void)lpCmdLine;
    (void)nCmdShow;

    wc.lpfnWndProc = WndProc;
    wc.lpszClassName = "TwoWindowClass";
    if (!RegisterClassA(&wc))
        return 1;

    g_primary = CreateWindowExA(0, wc.lpszClassName, "Two Window Primary",
                                WS_VISIBLE | WS_OVERLAPPEDWINDOW,
                                50, 50, 320, 200,
                                NULL, NULL, hInstance, NULL);
    if (!g_primary)
        return 1;

    g_secondary = CreateWindowExA(0, wc.lpszClassName, "Two Window Secondary",
                                  WS_VISIBLE | WS_OVERLAPPEDWINDOW,
                                  420, 50, 320, 200,
                                  NULL, NULL, hInstance, NULL);
    if (!g_secondary) {
        DestroyWindow(g_primary);
        return 1;
    }

    harness_signal("READY secondary");

    MSG msg;
    while (GetMessageA(&msg, 0, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageA(&msg);
    }
    return (int)msg.wParam;
}
