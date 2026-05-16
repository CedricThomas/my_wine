#include <windows.h>

static HWND g_primary = NULL;
static HWND g_secondary = NULL;
static int g_exit_code = 0;

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg) {
    case WM_CLOSE:
        if (hwnd == g_primary && g_secondary != NULL) {
            g_exit_code = 2;
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
            if (g_exit_code == 0 && g_primary != NULL)
                DestroyWindow(g_primary);
            return 0;
        }

        if (hwnd == g_primary) {
            g_primary = NULL;
            PostQuitMessage(g_exit_code);
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
    wc.lpszClassName = "TwoWindowClass32";
    if (!RegisterClassA(&wc))
        return 1;

    g_primary = CreateWindowExA(0, wc.lpszClassName, "Two Window Primary 32-bit",
                                WS_VISIBLE | WS_OVERLAPPEDWINDOW,
                                50, 50, 320, 200,
                                NULL, NULL, hInstance, NULL);
    if (!g_primary)
        return 1;

    g_secondary = CreateWindowExA(0, wc.lpszClassName, "Two Window Secondary 32-bit",
                                  WS_VISIBLE | WS_OVERLAPPEDWINDOW,
                                  420, 50, 320, 200,
                                  NULL, NULL, hInstance, NULL);
    if (!g_secondary) {
        DestroyWindow(g_primary);
        return 1;
    }

    MSG msg;
    while (GetMessageA(&msg, 0, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageA(&msg);
    }
    return (int)msg.wParam;
}
