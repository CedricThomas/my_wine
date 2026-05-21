#include <stdio.h>
#include <string.h>

#include "render_backend.h"
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
KERNEL32_ABI LONG_PTR GetWindowLongPtrA(HWND hwnd, int nIndex);
KERNEL32_ABI LONG_PTR SetWindowLongPtrA(HWND hwnd, int nIndex, LONG_PTR dwNewLong);
KERNEL32_ABI BOOL PostMessageA(HWND hWnd, UINT Msg, WPARAM wParam, LPARAM lParam);
KERNEL32_ABI void PostQuitMessage(int nExitCode);
KERNEL32_ABI BOOL PeekMessageA(MSG *lpMsg, HWND hWnd, UINT wMsgFilterMin,
                               UINT wMsgFilterMax, UINT wRemoveMsg);
KERNEL32_ABI BOOL GetMessageA(MSG *lpMsg, HWND hWnd, UINT wMsgFilterMin, UINT wMsgFilterMax);
KERNEL32_ABI HHOOK SetWindowsHookExA(int idHook, void *lpfn, HINSTANCE hMod, DWORD dwThreadId);
KERNEL32_ABI BOOL IsWindow(HWND hwnd);

static int g_failures = 0;
static int g_create_messages = 0;
static int g_nccreate_messages = 0;
static int g_close_messages = 0;
static int g_destroy_messages = 0;
static int g_ncdestroy_messages = 0;
static int g_focus_messages = 0;
static int g_killfocus_messages = 0;
static int g_activate_messages = 0;
static int g_send_messages = 0;
static int g_callproc_messages = 0;
static int g_create_payload = 0;
static int g_keyboard_hook_messages = 0;
static WPARAM g_keyboard_hook_wparam = 0;

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
#define PM_NOREMOVE           0x0000
#define PM_REMOVE             0x0001
#define WH_KEYBOARD           2
#define HC_ACTION             0

static LRESULT KERNEL32_ABI test_keyboard_hook(int nCode, WPARAM wParam, LPARAM lParam)
{
    (void)lParam;

    if (nCode == HC_ACTION) {
        g_keyboard_hook_messages++;
        g_keyboard_hook_wparam = wParam;
    }
    return 0;
}

static LRESULT KERNEL32_ABI test_wndproc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg) {
    case WM_NCCREATE:
        g_nccreate_messages++;
        return TRUE;
    case WM_CREATE: {
        CREATESTRUCTA *cs = (CREATESTRUCTA *)(intptr_t)lParam;
        g_create_messages++;
        if (cs && cs->lpCreateParams)
            g_create_payload = *(int *)cs->lpCreateParams;
        return 0;
    }
    case WM_ACTIVATE:
        g_activate_messages++;
        return 0;
    case WM_SETFOCUS:
        g_focus_messages++;
        return 0;
    case WM_KILLFOCUS:
        g_killfocus_messages++;
        return 0;
    case WM_CLOSE:
        g_close_messages++;
        return DefWindowProcA(hwnd, msg, wParam, lParam);
    case WM_DESTROY:
        g_destroy_messages++;
        return 0;
    case WM_NCDESTROY:
        g_ncdestroy_messages++;
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
    int create_payload = 77;
    memset(&wc, 0, sizeof(wc));
    wc.lpszClassName = "MessageDispatchTest";
    wc.lpfnWndProc = test_wndproc;

    T(RegisterClassA(&wc) != 0, "RegisterClassA failed");

    HWND hwnd = CreateWindowExA(0, wc.lpszClassName, "dispatch",
                                WS_OVERLAPPEDWINDOW | WS_VISIBLE,
                                0, 0, 320, 200, 0, 0, 0, &create_payload);
    HWND hwnd2 = CreateWindowExA(0, wc.lpszClassName, "dispatch-2",
                                 WS_OVERLAPPEDWINDOW | WS_VISIBLE,
                                 10, 10, 320, 200, 0, 0, 0, NULL);
    T(hwnd != 0, "CreateWindowExA failed");
    T(hwnd2 != 0, "CreateWindowExA second window failed");

    if (hwnd && hwnd2) {
        T(g_nccreate_messages == 2, "CreateWindowExA did not emit exactly one WM_NCCREATE per window");
        T(g_create_messages == 2, "CreateWindowExA did not emit exactly one WM_CREATE per window");
        T(g_create_payload == create_payload, "WM_CREATE did not receive lpCreateParams payload");
        T(g_focus_messages >= 2, "CreateWindowExA should activate/focus created windows");
        T(g_activate_messages >= 2, "CreateWindowExA should emit activation on created windows");

        T(SendMessageA(hwnd, TEST_SEND_MESSAGE, 0, 0) == TEST_SEND_RESULT,
          "SendMessageA did not return wndproc result");
        T(g_send_messages == 1, "SendMessageA did not reach wndproc");

        T(CallWindowProcA(test_wndproc, hwnd, TEST_CALLPROC_MESSAGE, 0, 0) == TEST_CALLPROC_RESULT,
          "CallWindowProcA did not return wndproc result");
        T(g_callproc_messages == 1, "CallWindowProcA did not reach wndproc");
        T(GetWindowLongPtrA(hwnd, GWLP_WNDPROC) == (LONG_PTR)(intptr_t)test_wndproc,
          "GetWindowLongPtrA did not preserve the full wndproc pointer");
        T(SetWindowLongPtrA(hwnd, GWLP_USERDATA, ((LONG_PTR)1 << 33) | 0x1234) == 0,
          "SetWindowLongPtrA should return previous userdata");
        T(GetWindowLongPtrA(hwnd, GWLP_USERDATA) == (((LONG_PTR)1 << 33) | 0x1234),
          "GetWindowLongPtrA did not preserve pointer-width userdata");

        MSG msg;
        memset(&msg, 0, sizeof(msg));
        msg.hwnd = hwnd;
        msg.message = WM_CLOSE;

        T(DispatchMessageA(&msg) == 0, "DispatchMessageA did not return WM_CLOSE result");
        T(g_close_messages == 1, "DispatchMessageA did not reach wndproc for WM_CLOSE");
        T(g_destroy_messages == 1, "WM_CLOSE default path did not send exactly one WM_DESTROY");
        T(g_ncdestroy_messages == 1, "WM_CLOSE default path did not send exactly one WM_NCDESTROY");
        T(GetActiveWindow() == hwnd2, "WM_CLOSE default path did not preserve the remaining active window");

        T(DestroyWindow(hwnd) == FALSE, "DestroyWindow should fail after WM_CLOSE destroyed the hwnd");

        T(PostMessageA(hwnd, TEST_SEND_MESSAGE, 1, 11) == FALSE,
          "PostMessageA should fail for a destroyed hwnd");
        T(PostMessageA(hwnd2, WM_LBUTTONDOWN, 0, 0) == TRUE,
          "PostMessageA failed for second window mouse message");
        T(PostMessageA(hwnd2, TEST_SEND_MESSAGE, 1, 11) == TRUE,
          "PostMessageA failed for second window custom message");
        T(PostMessageA((HWND)0xDEAD, TEST_SEND_MESSAGE, 0, 0) == FALSE,
          "PostMessageA should fail for an invalid hwnd");

        rb_msg_t backend_msg;
        memset(&backend_msg, 0, sizeof(backend_msg));
        backend_msg.hwnd = hwnd2;
        backend_msg.message = WM_MOUSEMOVE;
        backend_msg.pt_x = 7;
        backend_msg.pt_y = 9;
        T(rb_event_push(&backend_msg) == RB_OK,
          "rb_event_push failed for backend regression message");

        memset(&msg, 0, sizeof(msg));
        T(PeekMessageA(&msg, hwnd2, WM_MOUSEMOVE, WM_MOUSEMOVE, PM_NOREMOVE) == TRUE,
          "PeekMessageA(PM_NOREMOVE) did not expose backend-queued message");
        T(msg.hwnd == hwnd2, "PeekMessageA(PM_NOREMOVE) returned wrong backend hwnd");
        T(msg.message == WM_MOUSEMOVE, "PeekMessageA(PM_NOREMOVE) returned wrong backend message");

        memset(&msg, 0, sizeof(msg));
        T(PeekMessageA(&msg, hwnd2, WM_MOUSEMOVE, WM_MOUSEMOVE, PM_NOREMOVE) == TRUE,
          "PeekMessageA(PM_NOREMOVE) should not consume backend-queued message");
        T(msg.hwnd == hwnd2, "second PM_NOREMOVE returned wrong backend hwnd");
        T(msg.message == WM_MOUSEMOVE, "second PM_NOREMOVE returned wrong backend message");

        memset(&msg, 0, sizeof(msg));
        T(PeekMessageA(&msg, hwnd2, WM_MOUSEMOVE, WM_MOUSEMOVE, PM_REMOVE) == TRUE,
          "PeekMessageA(PM_REMOVE) did not consume backend-queued message");
        T(msg.hwnd == hwnd2, "PM_REMOVE returned wrong backend hwnd");
        T(msg.message == WM_MOUSEMOVE, "PM_REMOVE returned wrong backend message");

        memset(&msg, 0, sizeof(msg));
        T(PeekMessageA(&msg, hwnd2, 0, 0, PM_NOREMOVE) == TRUE,
          "PeekMessageA(hwnd filter) did not find the matching window message");
        T(msg.hwnd == hwnd2, "PeekMessageA(hwnd filter) returned wrong hwnd");
        T(msg.message == WM_LBUTTONDOWN, "PeekMessageA(hwnd filter) returned wrong message");

        memset(&msg, 0, sizeof(msg));
        T(PeekMessageA(&msg, hwnd2, 0, 0, PM_REMOVE) == TRUE,
          "PeekMessageA(hwnd filter, PM_REMOVE) did not remove the matching message");
        T(msg.hwnd == hwnd2, "PeekMessageA(hwnd filter, PM_REMOVE) returned wrong hwnd");
        T(msg.message == WM_LBUTTONDOWN, "PeekMessageA(hwnd filter, PM_REMOVE) returned wrong message");

        memset(&msg, 0, sizeof(msg));
        T(PeekMessageA(&msg, 0, TEST_SEND_MESSAGE, TEST_SEND_MESSAGE, PM_NOREMOVE) == TRUE,
          "PeekMessageA(range filter) did not find the matching message");
        T(msg.hwnd == hwnd2, "PeekMessageA(range filter) returned wrong hwnd");
        T(msg.message == TEST_SEND_MESSAGE, "PeekMessageA(range filter) returned wrong message");

        memset(&msg, 0, sizeof(msg));
        T(GetMessageA(&msg, hwnd2, 0, 0) == TRUE,
          "GetMessageA(hwnd filter) did not return the queued hwnd message");
        T(msg.hwnd == hwnd2, "GetMessageA(hwnd filter) returned wrong hwnd");
        T(msg.message == TEST_SEND_MESSAGE, "GetMessageA(hwnd filter) returned wrong message");

        memset(&msg, 0, sizeof(msg));
        T(PeekMessageA(&msg, 0, 0, 0, PM_NOREMOVE) == FALSE,
          "Queue should be empty after PM_REMOVE");

        T(SetWindowsHookExA(WH_KEYBOARD, test_keyboard_hook, 0, 0) != 0,
          "SetWindowsHookExA did not install WH_KEYBOARD hook");

        HWND hwnd_filter = CreateWindowExA(0, wc.lpszClassName, "dispatch-filter",
                                           WS_OVERLAPPEDWINDOW | WS_VISIBLE,
                                           20, 20, 320, 200, 0, 0, 0, NULL);
        T(hwnd_filter != 0, "CreateWindowExA filter window failed");

        memset(&backend_msg, 0, sizeof(backend_msg));
        backend_msg.hwnd = hwnd2;
        backend_msg.message = WM_KEYDOWN;
        backend_msg.wParam = VK_ESCAPE;
        backend_msg.lParam = 1;
        T(rb_event_push(&backend_msg) == RB_OK,
          "rb_event_push failed for backend key message");

        memset(&msg, 0, sizeof(msg));
        T(PeekMessageA(&msg, hwnd_filter, WM_KEYDOWN, WM_KEYDOWN, PM_REMOVE) == TRUE,
          "PeekMessageA should ignore a live mismatched hwnd filter for backend key messages");
        T(msg.hwnd == hwnd2, "mismatched-filter key message returned wrong hwnd");
        T(msg.message == WM_KEYDOWN, "mismatched-filter key message returned wrong message");
        T(msg.wParam == VK_ESCAPE, "mismatched-filter key message returned wrong VK");
        T(g_keyboard_hook_messages == 1, "WH_KEYBOARD hook did not see mismatched-filter key message");
        T(g_keyboard_hook_wparam == VK_ESCAPE, "WH_KEYBOARD hook received wrong VK");
        T(DestroyWindow(hwnd_filter) == TRUE, "DestroyWindow on filter window failed");

        PostQuitMessage(77);
        memset(&msg, 0, sizeof(msg));
        T(PeekMessageA(&msg, 0, 0, 0, PM_NOREMOVE) == TRUE,
          "PeekMessageA(PM_NOREMOVE) did not expose WM_QUIT");
        T(msg.message == WM_QUIT, "PeekMessageA(PM_NOREMOVE) returned wrong quit message");
        T(msg.wParam == 77, "PeekMessageA(PM_NOREMOVE) returned wrong quit code");

        memset(&msg, 0, sizeof(msg));
        T(PeekMessageA(&msg, 0, 0, 0, PM_REMOVE) == TRUE,
          "PeekMessageA(PM_REMOVE) did not consume WM_QUIT");
        T(msg.message == WM_QUIT, "PeekMessageA(PM_REMOVE) returned wrong quit message");
        T(msg.wParam == 77, "PeekMessageA(PM_REMOVE) returned wrong quit code");

        memset(&msg, 0, sizeof(msg));
        T(PeekMessageA(&msg, 0, 0, 0, PM_NOREMOVE) == FALSE,
          "Quit queue should be empty after WM_QUIT removal");

        msg.hwnd = hwnd2;
        msg.message = WM_CLOSE;
        T(DispatchMessageA(&msg) == 0, "DispatchMessageA did not close second window");
        T(IsWindow(hwnd2) == FALSE, "Second window should be destroyed by WM_CLOSE");
        T(g_ncdestroy_messages == 3, "Destroying both windows plus filter should send WM_NCDESTROY three times");

        memset(&msg, 0, sizeof(msg));
        T(PeekMessageA(&msg, 0, 0, 0, PM_NOREMOVE) == FALSE,
          "Destroying the last window should not synthesize WM_QUIT");

        HWND hwnd3 = CreateWindowExA(0, wc.lpszClassName, "dispatch-3",
                                     WS_OVERLAPPEDWINDOW | WS_VISIBLE,
                                     20, 20, 320, 200, 0, 0, 0, NULL);
        T(hwnd3 != 0, "CreateWindowExA should still work after all prior windows were destroyed");
        if (hwnd3)
            T(DestroyWindow(hwnd3) == TRUE, "DestroyWindow on recreated window failed");
    }

    if (hwnd2 && IsWindow(hwnd2))
        T(DestroyWindow(hwnd2) == TRUE, "DestroyWindow on second window failed");

    if (g_failures == 0)
        printf("PASS: user32 message dispatch\n");
    else
        printf("FAIL: %d test(s) failed\n", g_failures);

    return g_failures;
}
