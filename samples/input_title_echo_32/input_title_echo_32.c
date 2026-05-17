#include <windows.h>

static char g_input[64];
static char g_title[128];
static const char g_prefix[] = "Input Echo 32";

static void refresh_title(HWND hwnd)
{
    int out = 0;
    int in = 0;

    while (g_prefix[out] != '\0' && out < (int)(sizeof(g_title) - 1)) {
        g_title[out] = g_prefix[out];
        out++;
    }
    if (g_input[0] != '\0' && out < (int)(sizeof(g_title) - 1))
        g_title[out++] = ':';
    if (g_input[0] != '\0' && out < (int)(sizeof(g_title) - 1))
        g_title[out++] = ' ';
    while (g_input[in] != '\0' && out < (int)(sizeof(g_title) - 1)) {
        g_title[out++] = g_input[in++];
    }
    g_title[out] = '\0';
    SetWindowTextA(hwnd, g_title);
}

static void append_char(char ch)
{
    int len = 0;

    while (g_input[len] != '\0' && len < (int)(sizeof(g_input) - 1))
        len++;
    if (len >= (int)(sizeof(g_input) - 1))
        return;
    if ((unsigned char)ch < 32 || (unsigned char)ch > 126)
        return;

    g_input[len] = ch;
    g_input[len + 1] = '\0';
}

static void append_vk(WPARAM wParam)
{
    if ((wParam >= 'A' && wParam <= 'Z') || (wParam >= '0' && wParam <= '9'))
        append_char((char)wParam);
}

static void pop_char(void)
{
    int len = 0;

    while (g_input[len] != '\0' && len < (int)sizeof(g_input))
        len++;
    if (len > 0)
        g_input[len - 1] = '\0';
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    (void)lParam;

    switch (msg) {
    case WM_KEYDOWN:
        if (wParam == VK_BACK) {
            pop_char();
            refresh_title(hwnd);
            return 0;
        }
        append_vk(wParam);
        refresh_title(hwnd);
        return 0;
    case WM_CLOSE:
        DestroyWindow(hwnd);
        return 0;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }

    return DefWindowProcA(hwnd, msg, wParam, lParam);
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow)
{
    (void)hPrevInstance;
    (void)lpCmdLine;
    (void)nCmdShow;

    WNDCLASSA wc = {0};
    wc.lpfnWndProc = WndProc;
    wc.lpszClassName = "InputTitleEcho32Class";

    if (!RegisterClassA(&wc))
        return 1;

    HWND hwnd = CreateWindowExA(
        0,
        wc.lpszClassName,
        g_prefix,
        WS_VISIBLE | WS_OVERLAPPEDWINDOW,
        0,
        0,
        360,
        220,
        NULL,
        NULL,
        hInstance,
        NULL);

    if (!hwnd)
        return 1;

    refresh_title(hwnd);

    MSG msg;
    while (GetMessageA(&msg, 0, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageA(&msg);
    }
    return (int)msg.wParam;
}
