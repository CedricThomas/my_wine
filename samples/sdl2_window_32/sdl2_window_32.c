/*
 * sdl2_window_32.c — 32-bit user32 window sample for my_wine.
 *
 * Creates a Win32 window via RegisterClassA / CreateWindowExA and runs
 * the standard GetMessage/DispatchMessage loop. Exercises the user32
 * stubs and SDL2 backend under the PE32 runtime.
 *
 * Build: make samples SAMPLE=sdl2_window_32
 * Run:   ./scripts/samples.sh run sdl2_window_32
 */

#include <windows.h>

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

    HWND hwnd = CreateWindowExA(0, "TestClass", "Test 32-bit",
                                WS_VISIBLE | WS_OVERLAPPEDWINDOW,
                                0, 0, 320, 200,
                                NULL, NULL, hInstance, NULL);

    if (!hwnd)
        return 1;

    MSG msg;
    while (GetMessageA(&msg, 0, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageA(&msg);
    }
    return (int)msg.wParam;
}
