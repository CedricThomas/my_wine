#include <windows.h>

#include "../harness.h"

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_DESTROY:
            PostQuitMessage(0);
            break;
        default:
            return DefWindowProcA(hwnd, msg, wParam, lParam);
    }
    return 0;
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow) {
    (void)hPrevInstance;
    (void)lpCmdLine;
    (void)nCmdShow;

    WNDCLASSA wc = {0};
    wc.lpfnWndProc = WndProc;
    wc.lpszClassName = "TestClass";
    RegisterClassA(&wc);

    HWND hwnd = CreateWindowExA(0, "TestClass", "Test",
                                WS_VISIBLE | WS_OVERLAPPEDWINDOW,
                                0, 0, 320, 200,
                                NULL, NULL, hInstance, NULL);

    if (!hwnd)
        return 1;

    harness_signal("READY window");

    MSG msg;
    while (GetMessageA(&msg, 0, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageA(&msg);
    }
    return (int)msg.wParam;
}
