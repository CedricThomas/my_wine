/*
 * user32_weak_stub.c — Weak (overridable) no-op stubs for user32.dll.
 *
 * __attribute__((weak)) definitions provide linkable symbols for the default
 * my_wine64 build (which doesn't link SDL2).  When the real user32 stubs
 * (user32_window.c, user32_message.c, user32_input.c) are compiled, their
 * strong definitions override these.
 *
 * Every function returns a safe default (0, NULL, or FALSE).
 */

#include <stdint.h>

#define U32W __attribute__((weak))

/* ── Window lifecycle (user32_window.c) ─────────────────────── */

U32W int32_t RegisterClassA(void *wc) { (void)wc; return 0; }

U32W uintptr_t CreateWindowExA(uint32_t dwExStyle, const char *lpClassName,
    const char *lpWindowName, uint32_t dwStyle,
    int x, int y, int nWidth, int nHeight,
    uintptr_t hWndParent, uintptr_t hMenu,
    uintptr_t hInstance, void *lpParam)
{ (void)dwExStyle; (void)lpClassName; (void)lpWindowName; (void)dwStyle;
  (void)x; (void)y; (void)nWidth; (void)nHeight;
  (void)hWndParent; (void)hMenu; (void)hInstance; (void)lpParam; return 0; }

U32W int32_t DestroyWindow(uintptr_t hwnd) { (void)hwnd; return 0; }
U32W int32_t ShowWindow(uintptr_t hwnd, int nCmdShow) { (void)hwnd; (void)nCmdShow; return 0; }
U32W int32_t SetWindowPos(uintptr_t hwnd, uintptr_t hWndInsertAfter,
    int x, int y, int cx, int cy, uint32_t uFlags)
{ (void)hwnd; (void)hWndInsertAfter; (void)x; (void)y; (void)cx; (void)cy; (void)uFlags; return 0; }
U32W int32_t MoveWindow(uintptr_t hwnd, int x, int y, int w, int h, int32_t bRepaint)
{ (void)hwnd; (void)x; (void)y; (void)w; (void)h; (void)bRepaint; return 0; }
U32W int32_t SetWindowTextA(uintptr_t hwnd, const char *str) { (void)hwnd; (void)str; return 0; }
U32W int32_t GetWindowRect(uintptr_t hwnd, void *lpRect) { (void)hwnd; (void)lpRect; return 0; }
U32W int32_t GetClientRect(uintptr_t hwnd, void *lpRect) { (void)hwnd; (void)lpRect; return 0; }
U32W int32_t GetWindowLongA(uintptr_t hwnd, int nIndex) { (void)hwnd; (void)nIndex; return 0; }
U32W int32_t SetWindowLongA(uintptr_t hwnd, int nIndex, int32_t dwNewLong)
{ (void)hwnd; (void)nIndex; (void)dwNewLong; return 0; }
U32W intptr_t GetWindowLongPtrA(uintptr_t hwnd, int nIndex) { (void)hwnd; (void)nIndex; return 0; }
U32W intptr_t SetWindowLongPtrA(uintptr_t hwnd, int nIndex, intptr_t dwNewLong)
{ (void)hwnd; (void)nIndex; (void)dwNewLong; return 0; }
U32W int32_t IsWindow(uintptr_t hwnd) { (void)hwnd; return 0; }
U32W int32_t EnableWindow(uintptr_t hwnd, int32_t bEnable) { (void)hwnd; (void)bEnable; return 1; }
U32W uintptr_t GetDesktopWindow(void) { return 1; }
U32W uintptr_t GetActiveWindow(void) { return 1; }
U32W uintptr_t GetFocus(void) { return 1; }
U32W uintptr_t SetFocus(uintptr_t hwnd) { (void)hwnd; return 1; }
U32W int32_t UpdateWindow(uintptr_t hwnd) { (void)hwnd; return 1; }
U32W int32_t InvalidateRect(uintptr_t hwnd, const void *lpRect, int32_t bErase)
{ (void)hwnd; (void)lpRect; (void)bErase; return 1; }
U32W int32_t ValidateRect(uintptr_t hwnd, const void *lpRect)
{ (void)hwnd; (void)lpRect; return 1; }
U32W uintptr_t BeginPaint(uintptr_t hwnd, void *lpPaint) { (void)hwnd; (void)lpPaint; return 0; }
U32W int32_t EndPaint(uintptr_t hwnd, const void *lpPaint) { (void)hwnd; (void)lpPaint; return 1; }
U32W int MapWindowPoints(uintptr_t hf, uintptr_t ht, void *pts, uint32_t cnt)
{ (void)hf; (void)ht; (void)pts; (void)cnt; return 0; }
U32W int GetSystemMetrics(int nIndex) { (void)nIndex; return 0; }
U32W int32_t AdjustWindowRect(void *lpRect, uint32_t dwStyle, int32_t bMenu)
{ (void)lpRect; (void)dwStyle; (void)bMenu; return 1; }
U32W int32_t AdjustWindowRectEx(void *lpRect, uint32_t dwStyle, int32_t bMenu, uint32_t dwExStyle)
{ (void)lpRect; (void)dwStyle; (void)bMenu; (void)dwExStyle; return 1; }
U32W uintptr_t GetDC(uintptr_t hwnd) { (void)hwnd; return 0; }
U32W int ReleaseDC(uintptr_t hwnd, uintptr_t hdc) { (void)hwnd; (void)hdc; return 1; }

/* ── Message loop (user32_message.c) ─────────────────────────── */

U32W int32_t GetMessageA(void *lpMsg, uintptr_t hWnd, uint32_t wMin, uint32_t wMax)
{ (void)lpMsg; (void)hWnd; (void)wMin; (void)wMax; return 0; }
U32W int32_t PeekMessageA(void *lpMsg, uintptr_t hWnd, uint32_t wMin, uint32_t wMax, uint32_t wRemove)
{ (void)lpMsg; (void)hWnd; (void)wMin; (void)wMax; (void)wRemove; return 0; }
U32W int32_t TranslateMessage(const void *lpMsg) { (void)lpMsg; return 0; }
U32W int64_t DispatchMessageA(const void *lpMsg) { (void)lpMsg; return 0; }
U32W int32_t PostMessageA(uintptr_t hWnd, uint32_t Msg, uint32_t wParam, int32_t lParam)
{ (void)hWnd; (void)Msg; (void)wParam; (void)lParam; return 1; }
U32W void PostQuitMessage(int nExitCode) { (void)nExitCode; }
U32W int64_t SendMessageA(uintptr_t hWnd, uint32_t Msg, uint32_t wParam, int32_t lParam)
{ (void)hWnd; (void)Msg; (void)wParam; (void)lParam; return 0; }
U32W int64_t DefWindowProcA(uintptr_t hWnd, uint32_t Msg, uint32_t wParam, int32_t lParam)
{ (void)hWnd; (void)Msg; (void)wParam; (void)lParam; return 0; }
U32W int64_t CallWindowProcA(void *lpPrev, uintptr_t hWnd, uint32_t Msg, uint32_t wParam, int32_t lParam)
{ (void)lpPrev; (void)hWnd; (void)Msg; (void)wParam; (void)lParam; return 0; }
U32W uintptr_t SetWindowsHookExA(int idHook, void *lpfn, uintptr_t hMod, uint32_t dwThreadId)
{ (void)idHook; (void)lpfn; (void)hMod; (void)dwThreadId; return 0; }
U32W int32_t UnhookWindowsHookEx(uintptr_t hhk) { (void)hhk; return 1; }
U32W int64_t CallNextHookEx(uintptr_t hhk, int nCode, uint32_t wParam, int32_t lParam)
{ (void)hhk; (void)nCode; (void)wParam; (void)lParam; return 0; }
U32W int32_t SystemParametersInfoA(uint32_t uiAction, uint32_t uiParam, void *pvParam, uint32_t fWinIni)
{ (void)uiAction; (void)uiParam; (void)pvParam; (void)fWinIni; return 1; }

/* ── Input / cursor (user32_input.c) ─────────────────────────── */

U32W int16_t GetAsyncKeyState(int vKey) { (void)vKey; return 0; }
U32W uintptr_t LoadCursorA(uintptr_t hInst, const char *lpName)
{ (void)hInst; (void)lpName; return 0; }
U32W uintptr_t SetCursor(uintptr_t hCursor) { (void)hCursor; return 0; }
U32W int32_t SetCursorPos(int x, int y) { (void)x; (void)y; return 0; }
U32W int32_t ClipCursor(const void *lpRect) { (void)lpRect; return 1; }
U32W uintptr_t LoadIconA(uintptr_t hInst, const char *lpName)
{ (void)hInst; (void)lpName; return 0; }
U32W int wsprintfA(char *buf, const char *fmt, ...) { (void)buf; (void)fmt; return 0; }
U32W void SetRect(void *r, int x1, int y1, int x2, int y2)
{ if (r) { int *p = (int *)r; p[0]=x1; p[1]=y1; p[2]=x2; p[3]=y2; } }
