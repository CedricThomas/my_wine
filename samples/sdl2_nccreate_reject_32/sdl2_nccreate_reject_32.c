/*
 * sdl2_nccreate_reject_32.c — 32-bit WM_NCCREATE regression sample for my_wine.
 *
 * Proves both the default TRUE path and the app-overridable FALSE path of
 * WM_NCCREATE during window creation. Two window classes:
 *
 *   AcceptNCCreateClass — lets DefWindowProc handle WM_NCCREATE (returns TRUE).
 *                         Window creation succeeds.
 *   RejectNCCreateClass — explicitly returns FALSE from WM_NCCREATE.
 *                         Window creation is rejected and CreateWindowExA
 *                         returns NULL.
 *
 * Build: make samples SAMPLE=sdl2_nccreate_reject_32
 * Run:   ./scripts/samples.sh run sdl2_nccreate_reject_32
 */

#include <windows.h>

#include "../harness.h"

/* ── Class that rejects WM_NCCREATE ────────────────────────── */
LRESULT CALLBACK RejectNCCreateProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    (void)hwnd;
    (void)wParam;
    (void)lParam;

    if (msg == WM_NCCREATE)
        return FALSE;
    return TRUE;
}

/* ── Class that accepts WM_NCCREATE (default path) ────────── */
LRESULT CALLBACK AcceptNCCreateProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    (void)wParam;
    (void)lParam;

    switch (msg) {
    case WM_NCCREATE:
        return TRUE;
    case WM_CLOSE:
        DestroyWindow(hwnd);
        return 0;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    default:
        return DefWindowProcA(hwnd, msg, wParam, lParam);
    }
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow)
{
    (void)hPrevInstance;
    (void)lpCmdLine;
    (void)nCmdShow;

    WNDCLASSA wcReject = {0};
    WNDCLASSA wcAccept = {0};

    /* Register the rejecting class */
    wcReject.lpfnWndProc = RejectNCCreateProc;
    wcReject.lpszClassName = "RejectNCCreateClass";
    if (!RegisterClassA(&wcReject))
        return 1;

    /* Register the accepting class */
    wcAccept.lpfnWndProc = AcceptNCCreateProc;
    wcAccept.lpszClassName = "AcceptNCCreateClass";
    if (!RegisterClassA(&wcAccept))
        return 1;

    /* Attempt to create a window with the rejecting class — must fail */
    HWND rejected = CreateWindowExA(0, "RejectNCCreateClass", "Rejected",
                                    WS_OVERLAPPEDWINDOW,
                                    0, 0, 320, 200,
                                    NULL, NULL, hInstance, NULL);
    if (rejected != NULL) {
        /* Should never happen — the app explicitly rejected creation */
        DestroyWindow(rejected);
        return 2;
    }

    /* Create a window with the accepting class — must succeed */
    HWND accepted = CreateWindowExA(0, "AcceptNCCreateClass", "NCCreate Reject 32-bit",
                                    WS_VISIBLE | WS_OVERLAPPEDWINDOW,
                                    0, 0, 320, 200,
                                    NULL, NULL, hInstance, NULL);
    if (!accepted)
        return 1;

    harness_signal("READY accept");

    /* Run message loop for the accepted window */
    MSG msg;
    while (GetMessageA(&msg, 0, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageA(&msg);
    }
    return (int)msg.wParam;
}
