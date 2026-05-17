#include <windows.h>

static HWND g_window_a = NULL;
static HWND g_window_b = NULL;

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    (void)wParam;
    (void)lParam;

    switch (msg) {
    case WM_CLOSE:
        DestroyWindow(hwnd);
        return 0;

    case WM_DESTROY:
        if (hwnd == g_window_a) {
            g_window_a = NULL;
            if (g_window_b != NULL) {
                DestroyWindow(g_window_b);
                return 0;
            }
            PostQuitMessage(0);
            return 0;
        }

        if (hwnd == g_window_b) {
            g_window_b = NULL;
            if (g_window_a == NULL)
                PostQuitMessage(0);
            return 0;
        }
        return 0;

    default:
        return DefWindowProcA(hwnd, msg, wParam, lParam);
    }
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance,
                   LPSTR lpCmdLine, int nCmdShow)
{
    WNDCLASSA wc = {0};
    MSG msg;

    (void)hPrevInstance;
    (void)lpCmdLine;
    (void)nCmdShow;

    wc.lpfnWndProc = WndProc;
    wc.lpszClassName = "TwoWindowCloseChainClass";
    if (!RegisterClassA(&wc))
        return 1;

    g_window_a = CreateWindowExA(0, wc.lpszClassName, "Window A",
                                 WS_VISIBLE | WS_OVERLAPPEDWINDOW,
                                 50, 50, 320, 200,
                                 NULL, NULL, hInstance, NULL);
    if (!g_window_a)
        return 1;

    g_window_b = CreateWindowExA(0, wc.lpszClassName, "Window B",
                                 WS_VISIBLE | WS_OVERLAPPEDWINDOW,
                                 420, 50, 320, 200,
                                 NULL, NULL, hInstance, NULL);
    if (!g_window_b) {
        DestroyWindow(g_window_a);
        g_window_a = NULL;
        return 1;
    }

    while (GetMessageA(&msg, 0, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageA(&msg);
    }

    return (int)msg.wParam;
}
