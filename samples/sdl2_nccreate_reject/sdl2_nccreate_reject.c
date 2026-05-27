#include <windows.h>

#include "../harness.h"

/* RejectClass — returns FALSE on WM_NCCREATE to abort creation.
   Returns TRUE for all other messages (no further processing needed). */
LRESULT CALLBACK RejectProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    (void)hwnd;
    (void)wParam;
    (void)lParam;

    if (msg == WM_NCCREATE)
        return FALSE;  /* abort creation */
    return TRUE;
}

/* AcceptClass — returns TRUE on WM_NCCREATE, then handles the
   window normally via DefWindowProcA. */
LRESULT CALLBACK AcceptProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    (void)wParam;
    (void)lParam;

    switch (msg) {
    case WM_NCCREATE:
        return TRUE;  /* allow creation */
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    case WM_CLOSE:
        DestroyWindow(hwnd);
        return 0;
    default:
        return DefWindowProcA(hwnd, msg, wParam, lParam);
    }
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance,
                   LPSTR lpCmdLine, int nCmdShow)
{
    WNDCLASSA rejectClass = {0};
    WNDCLASSA acceptClass = {0};
    HWND hwnd;
    MSG msg;

    (void)hPrevInstance;
    (void)lpCmdLine;
    (void)nCmdShow;

    /* Register both window classes. */
    rejectClass.lpfnWndProc = RejectProc;
    rejectClass.lpszClassName = "RejectClass";
    if (!RegisterClassA(&rejectClass))
        return 1;

    acceptClass.lpfnWndProc = AcceptProc;
    acceptClass.lpszClassName = "AcceptClass";
    if (!RegisterClassA(&acceptClass))
        return 1;

    /* (a) CreateWindowExA with RejectClass — must return NULL. */
    {
        HWND rejected = CreateWindowExA(0, "RejectClass", "Should Not Appear",
                                        WS_OVERLAPPEDWINDOW,
                                        0, 0, 320, 200,
                                        NULL, NULL, hInstance, NULL);
        if (rejected != NULL)
            return 1;  /* assertion failure: should have been rejected */
    }

    /* (b) CreateWindowExA with AcceptClass — must return non-NULL. */
    hwnd = CreateWindowExA(0, "AcceptClass", "NCCreate Reject",
                           WS_VISIBLE | WS_OVERLAPPEDWINDOW,
                           0, 0, 320, 200,
                           NULL, NULL, hInstance, NULL);
    if (!hwnd)
        return 1;

    harness_signal("READY accept");

    /* (c) Standard message loop on the accepted window. */
    while (GetMessageA(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageA(&msg);
    }

    return (int)msg.wParam;
}
