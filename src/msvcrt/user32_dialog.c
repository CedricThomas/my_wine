#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "user32_priv.h"
#include "resource_win32.h"
#include "include/common.h"
#include "include/syscall_safe_utils.h"

#define WM_INITDIALOG 0x0110
#define WM_COMMAND 0x0111
#define WM_CLOSE 0x0010
#define CB_ADDSTRING 0x0143
#define CB_GETCOUNT 0x0146
#define CB_GETCURSEL 0x0147
#define CB_GETLBTEXT 0x0148
#define CB_GETLBTEXTLEN 0x0149
#define CB_INSERTSTRING 0x014a
#define CB_RESETCONTENT 0x014b
#define CB_FINDSTRING 0x014c
#define CB_SELECTSTRING 0x014d
#define CB_SETCURSEL 0x014e
#define CB_GETITEMDATA 0x0150
#define CB_SETITEMDATA 0x0151
#define LB_ADDSTRING 0x0180
#define LB_INSERTSTRING 0x0181
#define LB_DELETESTRING 0x0182
#define LB_RESETCONTENT 0x0184
#define LB_SETCURSEL 0x0186
#define LB_GETCURSEL 0x0188
#define LB_GETTEXT 0x0189
#define LB_GETTEXTLEN 0x018a
#define LB_GETCOUNT 0x018b
#define LB_SELECTSTRING 0x018c
#define LB_GETITEMDATA 0x0199
#define LB_SETITEMDATA 0x019a
#define LB_FINDSTRINGEXACT 0x01a2
#define UDM_SETRANGE 0x0465
#define UDM_SETPOS 0x0467
#define UDM_GETPOS 0x0468
#define UDM_SETBUDDY 0x0469
#define IDOK 1
#define BST_CHECKED 1

typedef intptr_t (KERNEL32_ABI *DLGPROC_WINE)(HWND, UINT, WPARAM, LPARAM);
typedef void (*DOOM95_REFRESH_MAPS_FN)(HWND);

extern void write_to_stderr(const char *msg);
extern LRESULT KERNEL32_ABI DefWindowProcA(HWND hWnd, UINT Msg, WPARAM wParam, LPARAM lParam);
extern ATOM KERNEL32_ABI RegisterClassA(const WNDCLASSA *lpWndClass);
extern BOOL KERNEL32_ABI DestroyWindow(HWND hWnd);
extern LRESULT KERNEL32_ABI SendMessageA(HWND hWnd, UINT Msg, WPARAM wParam, LPARAM lParam);
extern const char *wine_get_current_directory(void);
extern HWND KERNEL32_ABI CreateWindowExA(DWORD dwExStyle, const char *lpClassName,
                                         const char *lpWindowName, DWORD dwStyle, int X, int Y,
                                         int nWidth, int nHeight, HWND hWndParent, HMENU hMenu,
                                         HINSTANCE hInstance, void *lpParam);

typedef struct {
    HWND dialog;
    HWND handle;
    uint32_t id;
    uint32_t state;
    int32_t cur_sel;
    char text[128];
    char items[32][128];
    intptr_t item_data[32];
    uint32_t item_count;
    int32_t range_min;
    int32_t range_max;
    int32_t position;
    HWND buddy;
} dialog_item_state;

static dialog_item_state g_dialog_items[64];
static int g_dialog_class_registered = 0;
static uint32_t g_next_dialog_item_handle = 0x40000000u;

typedef struct {
    HWND dialog;
    intptr_t result;
    int ended;
} dialog_modal_state;

static dialog_modal_state g_dialog_modals[16];

static dialog_item_state *dialog_find_item(HWND dialog, uint32_t id, int create);
KERNEL32_STUB HWND GetDlgItem(HWND hDlg, int nIDDlgItem);
KERNEL32_STUB BOOL SetDlgItemTextA(HWND hDlg, int nIDDlgItem, const char *lpString);

static int dialog_item_append_string(dialog_item_state *item, const char *src)
{
    uint32_t idx;

    if (item->item_count >= 32)
        return -1;

    idx = item->item_count++;
    user32_strncpy(item->items[idx], src ? src : "", sizeof(item->items[idx]) - 1);
    item->items[idx][sizeof(item->items[idx]) - 1] = '\0';
    item->item_data[idx] = 0;
    if (item->cur_sel < 0) {
        item->cur_sel = 0;
        user32_strncpy(item->text, item->items[idx], sizeof(item->text) - 1);
        item->text[sizeof(item->text) - 1] = '\0';
    }
    return (int)idx;
}

static int dialog_item_insert_string(dialog_item_state *item, uint32_t idx, const char *src)
{
    uint32_t i;

    if (item->item_count >= 32)
        return -1;

    if (idx >= item->item_count)
        return dialog_item_append_string(item, src);

    for (i = item->item_count; i > idx; i--) {
        user32_memcpy(item->items[i], item->items[i - 1], sizeof(item->items[i]));
        item->item_data[i] = item->item_data[i - 1];
    }

    user32_strncpy(item->items[idx], src ? src : "", sizeof(item->items[idx]) - 1);
    item->items[idx][sizeof(item->items[idx]) - 1] = '\0';
    item->item_data[idx] = 0;
    item->item_count++;

    if (item->cur_sel < 0) {
        item->cur_sel = 0;
        user32_strncpy(item->text, item->items[idx], sizeof(item->text) - 1);
        item->text[sizeof(item->text) - 1] = '\0';
    } else if ((uint32_t)item->cur_sel >= idx)
        item->cur_sel++;

    return (int)idx;
}

static int dialog_item_delete_string(dialog_item_state *item, uint32_t idx)
{
    uint32_t i;

    if (!item || idx >= item->item_count)
        return -1;

    for (i = idx; i + 1 < item->item_count; i++) {
        user32_memcpy(item->items[i], item->items[i + 1], sizeof(item->items[i]));
        item->item_data[i] = item->item_data[i + 1];
    }

    if (item->item_count > 0)
        item->item_count--;

    if (item->item_count == 0)
        item->cur_sel = -1;
    else if (item->cur_sel == (int32_t)idx)
        item->cur_sel = -1;
    else if (item->cur_sel > (int32_t)idx)
        item->cur_sel--;
    if (item->cur_sel >= 0 && (uint32_t)item->cur_sel < item->item_count) {
        user32_strncpy(item->text, item->items[item->cur_sel], sizeof(item->text) - 1);
        item->text[sizeof(item->text) - 1] = '\0';
    } else {
        item->text[0] = '\0';
    }

    return (int)item->item_count;
}

static int dialog_item_find_string(dialog_item_state *item, uint32_t start_idx,
                                   const char *needle, int exact)
{
    uint32_t i;

    if (item->item_count == 0)
        return -1;
    if ((int32_t)start_idx < 0 || start_idx >= item->item_count)
        start_idx = 0;
    if (!needle)
        needle = "";

    for (i = start_idx; i < item->item_count; i++) {
        if ((exact && user32_strcmp(item->items[i], needle) == 0) ||
            (!exact && strstr(item->items[i], needle) != NULL))
            return (int)i;
    }
    for (i = 0; i < start_idx; i++) {
        if ((exact && user32_strcmp(item->items[i], needle) == 0) ||
            (!exact && strstr(item->items[i], needle) != NULL))
            return (int)i;
    }
    return -1;
}

static void dialog_try_doom95_seed_basewad_state(HINSTANCE hInstance)
{
    uintptr_t module_base = (uintptr_t)hInstance;
    uintptr_t *state_slot;
    uintptr_t state;
    char *basewad;
    const char *cwd;
    size_t cwd_len;

    if (module_base == 0)
        return;
    state_slot = (uintptr_t *)(module_base + 0x131e0u);

    state = *state_slot;
    if (state == 0)
        return;

    basewad = (char *)(uintptr_t)(state + 0x23u);
    cwd = wine_get_current_directory();
    cwd_len = cwd ? user32_strlen(cwd) : 0;
    if (cwd && cwd_len > 0 && cwd_len + 10 < 0x100u) {
        user32_strncpy(basewad, cwd, 0xff);
        if (basewad[cwd_len - 1] != '/') {
            basewad[cwd_len++] = '/';
            basewad[cwd_len] = '\0';
        }
        user32_strncpy(basewad + cwd_len, "DOOM1.WAD", 0xff - cwd_len);
        basewad[0xff] = '\0';
        return;
    }

    user32_strncpy(basewad, "DOOM1.WAD", 0xff);
    basewad[0xff] = '\0';
}

static void user32_dialog_ensure_class(void)
{
    WNDCLASSA cls;

    if (g_dialog_class_registered)
        return;

    DEBUG_LEVEL(1, "user32: dialog ensure class begin");
    memset(&cls, 0, sizeof(cls));
    cls.lpszClassName = "MY_WINE_DIALOG";
    cls.lpfnWndProc = (WNDPROC)DefWindowProcA;
    DEBUG_LEVEL(1, "user32: dialog ensure class register class=%s wndproc=%p",
                cls.lpszClassName, cls.lpfnWndProc);
    if (RegisterClassA(&cls) != 0)
        g_dialog_class_registered = 1;
    DEBUG_LEVEL(1, "user32: dialog ensure class registered=%d", g_dialog_class_registered);
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
            g_dialog_items[i].cur_sel = -1;
            g_dialog_items[i].handle = (HWND)(uintptr_t)(g_next_dialog_item_handle++);
            return &g_dialog_items[i];
        }
    }
    return NULL;
}

static dialog_item_state *dialog_find_item_by_handle(HWND handle)
{
    int i;

    for (i = 0; i < 64; i++) {
        if (g_dialog_items[i].handle == handle)
            return &g_dialog_items[i];
    }
    return NULL;
}

static int dialog_find_item_data_index(dialog_item_state *item, intptr_t needle)
{
    uint32_t i;

    if (!item)
        return -1;

    for (i = 0; i < item->item_count; i++) {
        if (item->item_data[i] == needle)
            return (int)i;
    }

    return -1;
}

static void dialog_try_doom95_autostart(HINSTANCE hInstance, HWND hwnd,
                                        const char *lpTemplateName, void *lpDialogFunc)
{
    dialog_item_state *provider_item;
    dialog_item_state *wad_item;
    dialog_item_state *map_item;
    HWND provider_hwnd;
    HWND wad_hwnd;
    HWND map_hwnd;
    HWND start_hwnd;
    WPARAM notify_wparam;
    int provider_idx;
    DOOM95_REFRESH_MAPS_FN refresh_maps;

    if ((uintptr_t)lpTemplateName != 0x72u || hwnd == 0 || lpDialogFunc == NULL)
        return;

    provider_item = dialog_find_item(hwnd, 0x3edu, 0);
    if (!provider_item)
        return;

    provider_idx = dialog_find_item_data_index(provider_item, 1);
    if (provider_idx < 0)
        return;

    provider_hwnd = provider_item->handle;
    wad_item = dialog_find_item(hwnd, 0x3f4u, 1);
    map_item = dialog_find_item(hwnd, 0x406u, 1);
    wad_hwnd = wad_item ? wad_item->handle : 0;
    map_hwnd = GetDlgItem(hwnd, 0x406);
    start_hwnd = GetDlgItem(hwnd, 0x3f1);
    refresh_maps = (DOOM95_REFRESH_MAPS_FN)((uintptr_t)hInstance + 0x8c30u);

    DEBUG_LEVEL(1, "user32: Doom95 launcher autostart select provider idx=%d", provider_idx);
    SendMessageA(provider_hwnd, CB_SETCURSEL, (WPARAM)provider_idx, 0);

    notify_wparam = (WPARAM)(0x3edu | (9u << 16));
    ((DLGPROC_WINE)lpDialogFunc)(hwnd, WM_COMMAND, notify_wparam,
                                 (LPARAM)(uintptr_t)provider_hwnd);

    if (wad_hwnd) {
        int wad_idx = -1;
        int current_wad_idx = -1;

        if (SendMessageA(wad_hwnd, CB_GETCOUNT, 0, 0) <= 0)
            SendMessageA(wad_hwnd, CB_ADDSTRING, 0, (LPARAM)(uintptr_t)"DOOM1.WAD");
        SendMessageA(wad_hwnd, WM_SETTEXT, 0, (LPARAM)(uintptr_t)"DOOM1.WAD");
        dialog_try_doom95_seed_basewad_state(hInstance);
        current_wad_idx = (int)SendMessageA(wad_hwnd, CB_GETCURSEL, 0, 0);
        if (current_wad_idx > 0) {
            wad_idx = current_wad_idx;
        } else if (wad_item && current_wad_idx == 0 &&
                   wad_item->item_count == 1 &&
                   user32_strcmp(wad_item->items[0], "(NONE)") != 0) {
            wad_idx = current_wad_idx;
        }
        if (wad_idx < 0 && wad_item)
            wad_idx = dialog_item_find_string(wad_item, 0, "DOOM1.WAD", 1);
        if (wad_idx < 0 && wad_item)
            wad_idx = dialog_item_find_string(wad_item, 0, "DOOM1", 1);
        if (wad_idx < 0 && wad_item) {
            uint32_t i;

            for (i = 0; i < wad_item->item_count; i++) {
                if (wad_item->item_data[i] != 0 &&
                    user32_strcmp(wad_item->items[i], "(NONE)") != 0) {
                    wad_idx = (int)i;
                    break;
                }
            }
        }
        if (wad_idx < 0 && SendMessageA(wad_hwnd, CB_GETCOUNT, 0, 0) > 0)
            wad_idx = 0;
        if (wad_idx >= 0) {
            SendMessageA(wad_hwnd, CB_SETCURSEL, (WPARAM)wad_idx, 0);
            ((DLGPROC_WINE)lpDialogFunc)(hwnd, WM_COMMAND,
                                         (WPARAM)(0x3f4u | (1u << 16)),
                                         (LPARAM)(uintptr_t)wad_hwnd);
        }
        if (refresh_maps != NULL)
            refresh_maps(hwnd);
        if (map_item && SendMessageA(map_hwnd, CB_GETCOUNT, 0, 0) <= 0) {
            SendMessageA(map_hwnd, CB_ADDSTRING, 0, (LPARAM)(uintptr_t)"E1M1");
            SendMessageA(map_hwnd, CB_SETITEMDATA, 0, 0);
        }
        if (map_hwnd && SendMessageA(map_hwnd, CB_GETCOUNT, 0, 0) > 0)
            SendMessageA(map_hwnd, CB_SETCURSEL, 0, 0);
        SetDlgItemTextA(hwnd, 0x436, "0");
        DEBUG_LEVEL(1, "user32: Doom95 launcher autostart seed base wad DOOM1.WAD");
    }

    DEBUG_LEVEL(1, "user32: Doom95 launcher autostart click start");
    ((DLGPROC_WINE)lpDialogFunc)(hwnd, WM_COMMAND, (WPARAM)0x3f1u,
                                 (LPARAM)(uintptr_t)start_hwnd);
}

static dialog_modal_state *dialog_find_modal(HWND dialog, int create)
{
    int i;

    for (i = 0; i < 16; i++) {
        if (g_dialog_modals[i].dialog == dialog)
            return &g_dialog_modals[i];
    }
    if (!create)
        return NULL;
    for (i = 0; i < 16; i++) {
        if (g_dialog_modals[i].dialog == 0) {
            user32_memset(&g_dialog_modals[i], 0, sizeof(g_dialog_modals[i]));
            g_dialog_modals[i].dialog = dialog;
            return &g_dialog_modals[i];
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

    DEBUG_LEVEL(1, "user32: CreateDialogParamA hInstance=0x%lx template=%p dlgproc=%p",
                (unsigned long)(uintptr_t)hInstance, (const void *)lpTemplateName, lpDialogFunc);

    if (!wine_resource_find((void *)(uintptr_t)hInstance, (const char *)(uintptr_t)5u,
                            lpTemplateName, NULL)) {
        DEBUG_LEVEL(1, "user32: CreateDialogParamA resource lookup failed");
        return 0;
    }

    user32_dialog_ensure_class();
    hwnd = CreateWindowExA(0, "MY_WINE_DIALOG", "Dialog", WS_POPUP | WS_CAPTION,
                           0, 0, 320, 200, hWndParent, 0, hInstance, NULL);
    if (hwnd)
        (void)dialog_find_modal(hwnd, 1);
    if (hwnd && lpDialogFunc) {
        DEBUG_LEVEL(1, "user32: CreateDialogParamA init hwnd=0x%lx",
                    (unsigned long)(uintptr_t)hwnd);
        ((DLGPROC_WINE)lpDialogFunc)(hwnd, WM_INITDIALOG, 0, dwInitParam);
        dialog_try_doom95_autostart(hInstance, hwnd, lpTemplateName, lpDialogFunc);
    }
    return hwnd;
}

BOOL user32_dialog_end(HWND hDlg, intptr_t nResult)
{
    dialog_modal_state *modal = dialog_find_modal(hDlg, 0);

    DEBUG_LEVEL(1, "user32: EndDialog hwnd=0x%lx result=%ld",
                (unsigned long)(uintptr_t)hDlg, (long)nResult);
    if (modal) {
        modal->ended = 1;
        modal->result = nResult;
    }
    if (hDlg != 0)
        DestroyWindow(hDlg);
    return TRUE;
}

int user32_dialog_run_modal(HWND hwnd, void *lpDialogFunc)
{
    dialog_modal_state *modal = dialog_find_modal(hwnd, 0);
    intptr_t result = 0;

    if (!hwnd)
        return 0;
    if (modal == NULL)
        modal = dialog_find_modal(hwnd, 1);
    if (modal == NULL)
        return 0;

    /*
     * Minimal modal behavior: after WM_INITDIALOG, drive the default OK path
     * so guest dialog procedures can perform their normal save/apply work
     * before calling EndDialog().
     */
    if (!modal->ended && lpDialogFunc)
        ((DLGPROC_WINE)lpDialogFunc)(hwnd, WM_COMMAND, (WPARAM)IDOK, 0);
    if (!modal->ended && lpDialogFunc)
        ((DLGPROC_WINE)lpDialogFunc)(hwnd, WM_CLOSE, 0, 0);
    if (!modal->ended)
        user32_dialog_end(hwnd, 0);

    result = modal->result;
    DEBUG_LEVEL(1, "user32: DialogBoxParamA hwnd=0x%lx result=%ld ended=%d",
                (unsigned long)(uintptr_t)hwnd, (long)result, modal->ended);
    user32_memset(modal, 0, sizeof(*modal));
    return (int)result;
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
    dialog_item_state *item = dialog_find_item(hDlg, (uint32_t)nIDDlgItem, 1);
    if (!item)
        return 0;
    DEBUG_LEVEL(2, "user32: GetDlgItem dialog=0x%lx id=0x%x -> handle=0x%lx",
                (unsigned long)(uintptr_t)hDlg, (unsigned)nIDDlgItem,
                (unsigned long)(uintptr_t)item->handle);
    return item->handle;
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
    item->text[sizeof(item->text) - 1] = '\0';
    DEBUG_LEVEL(1, "user32: SetDlgItemTextA dialog=0x%lx id=0x%x text='%s'",
                (unsigned long)(uintptr_t)hDlg, (unsigned)nIDDlgItem, item->text);
    return TRUE;
}

LONG_PTR user32_dialog_get_window_long_ptr(HWND hWnd, int nIndex)
{
    dialog_item_state *item = dialog_find_item_by_handle(hWnd);

    if (!item)
        return 0;

    switch (nIndex) {
    case GWL_ID:
        return (LONG_PTR)item->id;
    case GWL_HWNDPARENT:
        return (LONG_PTR)(uintptr_t)item->dialog;
    default:
        return 0;
    }
}

LRESULT user32_dialog_send_control_message(HWND hWnd, UINT Msg, WPARAM wParam, LPARAM lParam)
{
    dialog_item_state *item = dialog_find_item_by_handle(hWnd);

    if (!item)
        return (LRESULT)(intptr_t)-2147483647L;

    DEBUG_LEVEL(2,
                "user32: dialog ctrl hwnd=0x%lx id=0x%x msg=0x%x wp=0x%lx lp=0x%lx sel=%ld count=%u",
                (unsigned long)(uintptr_t)hWnd, (unsigned)item->id, (unsigned)Msg,
                (unsigned long)(uintptr_t)wParam, (unsigned long)(uintptr_t)lParam,
                (long)item->cur_sel, (unsigned)item->item_count);

    switch (Msg) {
    case WM_GETTEXT: {
        char *buf = (char *)(uintptr_t)lParam;
        size_t i;
        size_t max = (size_t)wParam;

        if (!buf || max == 0)
            return 0;
        for (i = 0; i + 1 < max && item->text[i] != '\0'; i++)
            buf[i] = item->text[i];
        buf[i] = '\0';
        return (LRESULT)i;
    }

    case WM_SETTEXT:
        if ((const char *)(uintptr_t)lParam == NULL)
            item->text[0] = '\0';
        else
            user32_strncpy(item->text, (const char *)(uintptr_t)lParam, sizeof(item->text) - 1);
        item->text[sizeof(item->text) - 1] = '\0';
        return TRUE;

    case CB_ADDSTRING:
    case LB_ADDSTRING: {
        int idx = dialog_item_append_string(item, (const char *)(uintptr_t)lParam);

        if (idx < 0)
            return -1;
        DEBUG_LEVEL(2, "user32: dialog ctrl id=0x%x add[%u] text='%s'",
                    (unsigned)item->id, (unsigned)idx, item->items[idx]);
        return (LRESULT)idx;
    }

    case CB_INSERTSTRING:
    case LB_INSERTSTRING: {
        int idx = dialog_item_insert_string(item, (uint32_t)wParam,
                                            (const char *)(uintptr_t)lParam);

        if (idx < 0)
            return -1;
        DEBUG_LEVEL(2, "user32: dialog ctrl id=0x%x insert req=%ld -> [%u] text='%s'",
                    (unsigned)item->id, (long)(int32_t)wParam, (unsigned)idx,
                    item->items[idx]);
        return (LRESULT)idx;
    }

    case LB_DELETESTRING: {
        int count = dialog_item_delete_string(item, (uint32_t)wParam);

        DEBUG_LEVEL(2, "user32: dialog ctrl id=0x%x delete idx=%ld -> count=%d",
                    (unsigned)item->id, (long)(int32_t)wParam, count);
        return (LRESULT)count;
    }

    case CB_GETCOUNT:
    case LB_GETCOUNT:
        return (LRESULT)item->item_count;

    case CB_GETCURSEL:
    case LB_GETCURSEL:
        return (LRESULT)item->cur_sel;

    case CB_GETLBTEXT:
    case LB_GETTEXT: {
        uint32_t idx = (uint32_t)wParam;
        char *buf = (char *)(uintptr_t)lParam;

        if (idx >= item->item_count || !buf)
            return -1;
        user32_strncpy(buf, item->items[idx], 128);
        return (LRESULT)user32_strlen(item->items[idx]);
    }

    case CB_GETLBTEXTLEN:
    case LB_GETTEXTLEN: {
        uint32_t idx = (uint32_t)wParam;

        if (idx >= item->item_count)
            return -1;
        return (LRESULT)user32_strlen(item->items[idx]);
    }

    case CB_RESETCONTENT:
    case LB_RESETCONTENT:
        item->item_count = 0;
        item->cur_sel = -1;
        item->text[0] = '\0';
        return TRUE;

    case CB_FINDSTRING: {
        int idx = dialog_item_find_string(item, (uint32_t)wParam,
                                          (const char *)(uintptr_t)lParam, 0);

        DEBUG_LEVEL(2, "user32: dialog ctrl id=0x%x find start=%ld text='%s' -> %d",
                    (unsigned)item->id, (long)(int32_t)wParam,
                    (const char *)(uintptr_t)lParam ? (const char *)(uintptr_t)lParam : "",
                    idx);
        return (LRESULT)idx;
    }

    case LB_FINDSTRINGEXACT: {
        int idx = dialog_item_find_string(item, (uint32_t)wParam,
                                          (const char *)(uintptr_t)lParam, 1);

        DEBUG_LEVEL(2, "user32: dialog ctrl id=0x%x find exact start=%ld text='%s' -> %d",
                    (unsigned)item->id, (long)(int32_t)wParam,
                    (const char *)(uintptr_t)lParam ? (const char *)(uintptr_t)lParam : "",
                    idx);
        return (LRESULT)idx;
    }

    case CB_SELECTSTRING:
    case LB_SELECTSTRING: {
        int idx = dialog_item_find_string(item, (uint32_t)wParam,
                                          (const char *)(uintptr_t)lParam, 0);

        if (idx < 0)
            return -1;
        item->cur_sel = idx;
        user32_strncpy(item->text, item->items[idx], sizeof(item->text) - 1);
        item->text[sizeof(item->text) - 1] = '\0';
        DEBUG_LEVEL(2, "user32: dialog ctrl id=0x%x select string -> %d",
                    (unsigned)item->id, idx);
        return (LRESULT)idx;
    }

    case CB_SETCURSEL:
    case LB_SETCURSEL:
        if ((int32_t)wParam < 0) {
            item->cur_sel = -1;
            return -1;
        }
        if ((uint32_t)wParam >= item->item_count)
            return -1;
        item->cur_sel = (int32_t)wParam;
        user32_strncpy(item->text, item->items[item->cur_sel], sizeof(item->text) - 1);
        item->text[sizeof(item->text) - 1] = '\0';
        DEBUG_LEVEL(2, "user32: dialog ctrl id=0x%x set sel=%ld text='%s'",
                    (unsigned)item->id, (long)item->cur_sel,
                    item->items[item->cur_sel]);
        return (LRESULT)item->cur_sel;

    case CB_GETITEMDATA: {
        uint32_t idx = (uint32_t)wParam;

        if (idx >= item->item_count) {
            DEBUG_LEVEL(1, "user32: dialog ctrl id=0x%x get item data bad idx=%u count=%u",
                        (unsigned)item->id, (unsigned)idx, (unsigned)item->item_count);
            return 0;
        }
        DEBUG_LEVEL(2, "user32: dialog ctrl id=0x%x get item data[%u]=0x%lx",
                    (unsigned)item->id, (unsigned)idx,
                    (unsigned long)item->item_data[idx]);
        return (LRESULT)item->item_data[idx];
    }

    case LB_GETITEMDATA: {
        uint32_t idx = (uint32_t)wParam;

        if (idx >= item->item_count) {
            DEBUG_LEVEL(1, "user32: dialog ctrl id=0x%x lb get item data bad idx=%u count=%u",
                        (unsigned)item->id, (unsigned)idx, (unsigned)item->item_count);
            return -1;
        }
        DEBUG_LEVEL(2, "user32: dialog ctrl id=0x%x lb get item data[%u]=0x%lx",
                    (unsigned)item->id, (unsigned)idx,
                    (unsigned long)item->item_data[idx]);
        return (LRESULT)item->item_data[idx];
    }

    case CB_SETITEMDATA: {
        uint32_t idx = (uint32_t)wParam;

        if (idx >= item->item_count)
            return -1;
        item->item_data[idx] = (intptr_t)lParam;
        DEBUG_LEVEL(2, "user32: dialog ctrl id=0x%x set item data[%u]=0x%lx",
                    (unsigned)item->id, (unsigned)idx,
                    (unsigned long)item->item_data[idx]);
        return TRUE;
    }

    case LB_SETITEMDATA: {
        uint32_t idx = (uint32_t)wParam;

        if (idx >= item->item_count)
            return -1;
        item->item_data[idx] = (intptr_t)lParam;
        DEBUG_LEVEL(2, "user32: dialog ctrl id=0x%x lb set item data[%u]=0x%lx",
                    (unsigned)item->id, (unsigned)idx,
                    (unsigned long)item->item_data[idx]);
        return TRUE;
    }

    case UDM_SETRANGE:
        item->range_max = (int16_t)(uint16_t)(lParam & 0xffff);
        item->range_min = (int16_t)(uint16_t)((lParam >> 16) & 0xffff);
        DEBUG_LEVEL(2, "user32: dialog ctrl id=0x%x set range min=%ld max=%ld",
                    (unsigned)item->id, (long)item->range_min, (long)item->range_max);
        return 0;

    case UDM_SETPOS: {
        LRESULT old_pos = (LRESULT)item->position;
        item->position = (int16_t)(uint16_t)(lParam & 0xffff);
        if (item->buddy) {
            dialog_item_state *buddy = dialog_find_item_by_handle(item->buddy);

            if (buddy) {
                snprintf(buddy->text, sizeof(buddy->text), "%ld", (long)item->position);
                buddy->text[sizeof(buddy->text) - 1] = '\0';
            }
        }
        DEBUG_LEVEL(2, "user32: dialog ctrl id=0x%x set pos=%ld old=%ld",
                    (unsigned)item->id, (long)item->position, (long)old_pos);
        return old_pos;
    }

    case UDM_GETPOS:
        return (LRESULT)(uint16_t)item->position;

    case UDM_SETBUDDY: {
        HWND old_buddy = item->buddy;
        item->buddy = (HWND)(uintptr_t)wParam;
        DEBUG_LEVEL(2, "user32: dialog ctrl id=0x%x set buddy=0x%lx old=0x%lx",
                    (unsigned)item->id,
                    (unsigned long)(uintptr_t)item->buddy,
                    (unsigned long)(uintptr_t)old_buddy);
        return (LRESULT)(uintptr_t)old_buddy;
    }

    default:
        return 0;
    }
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
    (void)uType;
    fprintf(stderr, "MessageBoxA invoked: caption='%s' text='%s'\n",
            lpCaption ? lpCaption : "",
            lpText ? lpText : "");
    return IDOK;
}
