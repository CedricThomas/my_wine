#include <stdio.h>
#include <string.h>

#include "user32_types.h"

KERNEL32_ABI HWND GetDlgItem(HWND hDlg, int nIDDlgItem);
KERNEL32_ABI LRESULT SendMessageA(HWND hWnd, UINT Msg, WPARAM wParam, LPARAM lParam);
KERNEL32_ABI LONG_PTR GetWindowLongPtrA(HWND hwnd, int nIndex);

static int g_failures = 0;

#define T(cond, msg)                                                           \
    do {                                                                       \
        if (!(cond)) {                                                         \
            fprintf(stderr, "FAIL: %s\n", msg);                                \
            g_failures++;                                                      \
        }                                                                      \
    } while (0)

#define CB_ADDSTRING     0x0143
#define CB_GETCOUNT      0x0146
#define CB_GETCURSEL     0x0147
#define CB_GETLBTEXT     0x0148
#define CB_GETLBTEXTLEN  0x0149
#define CB_INSERTSTRING  0x014a
#define CB_RESETCONTENT  0x014b
#define CB_FINDSTRING    0x014c
#define CB_SELECTSTRING  0x014d
#define CB_SETCURSEL     0x014e
#define CB_GETITEMDATA   0x0150
#define CB_SETITEMDATA   0x0151
#define LB_ADDSTRING     0x0180
#define LB_DELETESTRING  0x0182
#define LB_FINDSTRINGEXACT 0x01a2
#define UDM_SETRANGE     0x0465
#define UDM_SETPOS       0x0467
#define UDM_GETPOS       0x0468
#define UDM_SETBUDDY     0x0469

int main(void)
{
    HWND dialog = (HWND)(uintptr_t)0x12340000u;
    HWND combo = GetDlgItem(dialog, 0x40);
    HWND buddy = GetDlgItem(dialog, 0x41);
    HWND spinner = GetDlgItem(dialog, 0x42);
    char buf[128];

    T(combo != 0, "GetDlgItem should allocate a dialog item handle");
    T(GetWindowLongPtrA(combo, GWL_ID) == 0x40, "dialog item GWL_ID mismatch");
    T(GetWindowLongPtrA(combo, GWL_HWNDPARENT) == (LONG_PTR)(uintptr_t)dialog,
      "dialog item parent mismatch");

    T(SendMessageA(combo, CB_ADDSTRING, 0, (LPARAM)(uintptr_t)"alpha") == 0,
      "CB_ADDSTRING should append first item");
    T(SendMessageA(combo, CB_ADDSTRING, 0, (LPARAM)(uintptr_t)"beta") == 1,
      "CB_ADDSTRING should append second item");
    T(SendMessageA(combo, CB_INSERTSTRING, 0, (LPARAM)(uintptr_t)"zero") == 0,
      "CB_INSERTSTRING should insert at front");
    T(SendMessageA(combo, CB_GETCOUNT, 0, 0) == 3,
      "CB_GETCOUNT should report inserted item count");
    T(SendMessageA(combo, CB_FINDSTRING, 0, (LPARAM)(uintptr_t)"bet") == 2,
      "CB_FINDSTRING should match substring");
    T(SendMessageA(combo, LB_FINDSTRINGEXACT, 0, (LPARAM)(uintptr_t)"alpha") == 1,
      "LB_FINDSTRINGEXACT should match exact string");
    T(SendMessageA(combo, CB_GETLBTEXTLEN, 2, 0) == 4,
      "CB_GETLBTEXTLEN should return item length");

    memset(buf, 0, sizeof(buf));
    T(SendMessageA(combo, CB_GETLBTEXT, 1, (LPARAM)(uintptr_t)buf) == 5,
      "CB_GETLBTEXT should return copied string length");
    T(strcmp(buf, "alpha") == 0, "CB_GETLBTEXT copied wrong string");

    T(SendMessageA(combo, CB_SELECTSTRING, 0, (LPARAM)(uintptr_t)"beta") == 2,
      "CB_SELECTSTRING should update current selection");
    T(SendMessageA(combo, CB_GETCURSEL, 0, 0) == 2,
      "CB_GETCURSEL should report selected item");

    memset(buf, 0, sizeof(buf));
    T(SendMessageA(combo, WM_GETTEXT, sizeof(buf), (LPARAM)(uintptr_t)buf) == 4,
      "WM_GETTEXT should return selected text length");
    T(strcmp(buf, "beta") == 0, "WM_GETTEXT should expose selected text");

    T(SendMessageA(combo, CB_SETITEMDATA, 2, (LPARAM)0x55) == TRUE,
      "CB_SETITEMDATA should store per-item data");
    T(SendMessageA(combo, CB_GETITEMDATA, 2, 0) == 0x55,
      "CB_GETITEMDATA should return stored per-item data");

    T(SendMessageA(combo, LB_DELETESTRING, 1, 0) == 2,
      "LB_DELETESTRING should reduce item count");
    T(SendMessageA(combo, CB_SETCURSEL, 1, 0) == 1,
      "CB_SETCURSEL should select the remaining last item");

    memset(buf, 0, sizeof(buf));
    T(SendMessageA(combo, WM_GETTEXT, sizeof(buf), (LPARAM)(uintptr_t)buf) == 4,
      "WM_GETTEXT should reflect selection after delete");
    T(strcmp(buf, "beta") == 0, "selection text changed unexpectedly after delete");

    T(SendMessageA(spinner, UDM_SETBUDDY, (WPARAM)(uintptr_t)buddy, 0) == 0,
      "UDM_SETBUDDY should return no previous buddy initially");
    T(SendMessageA(spinner, UDM_SETRANGE, 0, (LPARAM)((1u << 16) | 99u)) == 0,
      "UDM_SETRANGE should succeed");
    T(SendMessageA(spinner, UDM_SETPOS, 0, 42) == 0,
      "UDM_SETPOS should return the previous position");
    T(SendMessageA(spinner, UDM_GETPOS, 0, 0) == 42,
      "UDM_GETPOS should return the stored position");

    memset(buf, 0, sizeof(buf));
    T(SendMessageA(buddy, WM_GETTEXT, sizeof(buf), (LPARAM)(uintptr_t)buf) == 2,
      "buddy WM_GETTEXT should reflect spinner position");
    T(strcmp(buf, "42") == 0, "buddy text should be updated from spinner");

    T(SendMessageA(combo, CB_RESETCONTENT, 0, 0) == TRUE,
      "CB_RESETCONTENT should clear item storage");
    T(SendMessageA(combo, CB_GETCOUNT, 0, 0) == 0,
      "CB_RESETCONTENT should leave an empty control");

    if (g_failures == 0)
        printf("PASS: user32 dialog controls\n");
    else
        printf("FAIL: %d test(s) failed\n", g_failures);

    return g_failures;
}
