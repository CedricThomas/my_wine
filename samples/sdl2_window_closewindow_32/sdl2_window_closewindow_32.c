#include <windows.h>

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg) {
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;

    default:
        return DefWindowProcA(hwnd, msg, wParam, lParam);
    }
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance,
                   LPSTR lpCmdLine, int nCmdShow)
{
    WNDCLASSA wc = {0};
    HWND hwnd;
    MSG msg;

    (void)hPrevInstance;
    (void)lpCmdLine;
    (void)nCmdShow;

    wc.lpfnWndProc = WndProc;
    wc.lpszClassName = "CloseWindowClass32";
    if (!RegisterClassA(&wc))
        return 1;

    hwnd = CreateWindowExA(0, wc.lpszClassName, "CloseWindow Sample 32-bit",
                           WS_VISIBLE | WS_OVERLAPPEDWINDOW,
                           0, 0, 320, 200,
                           NULL, NULL, hInstance, NULL);
    if (!hwnd)
        return 1;

    while (GetMessageA(&msg, 0, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageA(&msg);
    }

    return (int)msg.wParam;
}
