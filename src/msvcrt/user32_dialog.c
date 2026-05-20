#include <stdint.h>
#include <string.h>

#include "user32_priv.h"
#include "resource_win32.h"
#include "include/common.h"
#include "include/syscall_safe_utils.h"

#define WM_INITDIALOG 0x0110
#define IDOK 1
#define BST_CHECKED 1

typedef intptr_t (KERNEL32_ABI *DLGPROC_WINE)(HWND, UINT, WPARAM, LPARAM);

extern void write_to_stderr(const char *msg);
extern LRESULT KERNEL32_ABI DefWindowProcA(HWND hWnd, UINT Msg, WPARAM wParam, LPARAM lParam);
extern ATOM RegisterClassA(const WNDCLASSA *lpWndClass);
extern HWND CreateWindowExA(DWORD dwExStyle, const char *lpClassName, const char *lpWindowName,
                            DWORD dwStyle, int X, int Y, int nWidth, int nHeight,
                            HWND hWndParent, HMENU hMenu, HINSTANCE hInstance, void *lpParam);

typedef struct {
    HWND dialog;
    uint32_t id;
    uint32_t state;
    char text[128];
} dialog_item_state;

static dialog_item_state g_dialog_items[64];
static int g_dialog_class_registered = 0;

static void user32_dialog_ensure_class(void)
{
    WNDCLASSA cls;

    if (g_dialog_class_registered)
        return;

    memset(&cls, 0, sizeof(cls));
    cls.lpszClassName = "MY_WINE_DIALOG";
    cls.lpfnWndProc = (WNDPROC)DefWindowProcA;
    if (RegisterClassA(&cls) != 0)
        g_dialog_class_registered = 1;
}

static dialog_item_state *dialog_find_item(HWND dialog, uint32_t id, int create)
{
    int i;

    for (i = 0; i < 64; i++) {
        if (g_dialog_items[i].dialog == dialog && g_dialog_items[i].id == id)
            return &g_dialog_items[i];
    }
    if (!create)
        return NULL;
    for (i = 0; i < 64; i++) {
        if (g_dialog_items[i].dialog == 0) {
            memset(&g_dialog_items[i], 0, sizeof(g_dialog_items[i]));
            g_dialog_items[i].dialog = dialog;
            g_dialog_items[i].id = id;
            return &g_dialog_items[i];
        }
    }
    return NULL;
}

KERNEL32_STUB
HWND CreateDialogParamA(HINSTANCE hInstance, const char *lpTemplateName, HWND hWndParent,
                        void *lpDialogFunc, LPARAM dwInitParam)
{
    HWND hwnd;
    (void)hInstance;

    if (!wine_resource_find(NULL, (const char *)(uintptr_t)5u, lpTemplateName, NULL))
        return 0;

    user32_dialog_ensure_class();
    hwnd = CreateWindowExA(0, "MY_WINE_DIALOG", "Dialog", WS_POPUP | WS_CAPTION,
                           0, 0, 320, 200, hWndParent, 0, hInstance, NULL);
    if (hwnd && lpDialogFunc)
        ((DLGPROC_WINE)lpDialogFunc)(hwnd, WM_INITDIALOG, 0, dwInitParam);
    return hwnd;
}

KERNEL32_STUB
BOOL IsDialogMessageA(HWND hDlg, MSG *lpMsg)
{
    (void)hDlg;
    (void)lpMsg;
    return FALSE;
}

KERNEL32_STUB
HWND GetDlgItem(HWND hDlg, int nIDDlgItem)
{
    dialog_item_state *item = dialog_find_item(hDlg, (uint32_t)nIDDlgItem, 0);
    if (!item)
        return 0;
    return (HWND)(uintptr_t)(0x40000000u | ((uint32_t)nIDDlgItem & 0xffffu));
}

KERNEL32_STUB
BOOL CheckDlgButton(HWND hDlg, int nIDButton, UINT uCheck)
{
    dialog_item_state *item = dialog_find_item(hDlg, (uint32_t)nIDButton, 1);
    if (!item)
        return FALSE;
    item->state = uCheck;
    return TRUE;
}

KERNEL32_STUB
UINT IsDlgButtonChecked(HWND hDlg, int nIDButton)
{
    dialog_item_state *item = dialog_find_item(hDlg, (uint32_t)nIDButton, 0);
    return item ? item->state : 0;
}

KERNEL32_STUB
BOOL SetDlgItemTextA(HWND hDlg, int nIDDlgItem, const char *lpString)
{
    dialog_item_state *item = dialog_find_item(hDlg, (uint32_t)nIDDlgItem, 1);
    if (!item)
        return FALSE;
    if (!lpString)
        lpString = "";
    strncpy(item->text, lpString, sizeof(item->text) - 1);
    return TRUE;
}

KERNEL32_STUB
int LoadStringA(HINSTANCE hInstance, uint32_t uID, char *lpBuffer, int cchBufferMax)
{
    void *res;
    const uint16_t *table;
    uint32_t size = 0;
    uint32_t block_id = (uID / 16u) + 1u;
    uint32_t entry_id = uID % 16u;
    uint32_t i;
    uint16_t len;

    if (!lpBuffer || cchBufferMax <= 0)
        return 0;

    res = wine_resource_find((void *)(uintptr_t)hInstance, (const char *)(uintptr_t)6u,
                             (const char *)(uintptr_t)block_id, &size);
    if (!res)
        return 0;

    table = (const uint16_t *)wine_resource_lock(res);
    if (!table || size < sizeof(uint16_t))
        return 0;

    for (i = 0; i < entry_id; i++) {
        if ((const uint8_t *)table + sizeof(uint16_t) > (const uint8_t *)wine_resource_lock(res) + size)
            return 0;
        len = *table++;
        if ((const uint8_t *)(table + len) > (const uint8_t *)wine_resource_lock(res) + size)
            return 0;
        table += len;
    }

    len = *table++;
    if (len >= (uint16_t)cchBufferMax)
        len = (uint16_t)(cchBufferMax - 1);
    for (i = 0; i < len; i++) {
        uint16_t ch = table[i];
        lpBuffer[i] = (ch <= 0x7f) ? (char)ch : '?';
    }
    lpBuffer[len] = '\0';
    return (int)len;
}

KERNEL32_STUB
int MessageBoxA(HWND hWnd, const char *lpText, const char *lpCaption, UINT uType)
{
    (void)hWnd;
    (void)lpText;
    (void)lpCaption;
    (void)uType;
    write_to_stderr("MessageBoxA invoked\n");
    return IDOK;
}
