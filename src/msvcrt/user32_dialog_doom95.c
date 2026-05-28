#include <stdint.h>

#include "kernel32_doom95.h"
#include "user32_dialog_priv.h"
#include "include/debug.h"

#define WM_COMMAND 0x0111
#define CB_ADDSTRING 0x0143
#define CB_GETCOUNT 0x0146
#define CB_GETCURSEL 0x0147
#define CB_SETCURSEL 0x014e
#define CB_SETITEMDATA 0x0151

typedef void (*DOOM95_REFRESH_MAPS_FN)(HWND);

extern LRESULT KERNEL32_ABI SendMessageA(HWND hWnd, UINT Msg, WPARAM wParam, LPARAM lParam);
extern BOOL KERNEL32_ABI DestroyWindow(HWND hWnd);

void user32_dialog_try_doom95_autostart(HINSTANCE hInstance, HWND hwnd,
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

    provider_item = user32_dialog_find_item(hwnd, 0x3edu, 0);
    if (!provider_item)
        return;

    provider_idx = user32_dialog_find_item_data_index(provider_item, 1);
    if (provider_idx < 0)
        return;

    provider_hwnd = provider_item->handle;
    wad_item = user32_dialog_find_item(hwnd, 0x3f4u, 1);
    map_item = user32_dialog_find_item(hwnd, 0x406u, 1);
    wad_hwnd = wad_item ? wad_item->handle : 0;
    map_hwnd = user32_dialog_get_item_handle(hwnd, 0x406);
    start_hwnd = user32_dialog_get_item_handle(hwnd, 0x3f1);
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
        wine_doom95_seed_basewad_state((uintptr_t)hInstance);
        current_wad_idx = (int)SendMessageA(wad_hwnd, CB_GETCURSEL, 0, 0);
        if (current_wad_idx > 0) {
            wad_idx = current_wad_idx;
        } else if (wad_item && current_wad_idx == 0 &&
                   wad_item->item_count == 1 &&
                   user32_strcmp(wad_item->items[0], "(NONE)") != 0) {
            wad_idx = current_wad_idx;
        }
        if (wad_idx < 0 && wad_item)
            wad_idx = user32_dialog_find_string(wad_item, 0, "DOOM1.WAD", 1);
        if (wad_idx < 0 && wad_item)
            wad_idx = user32_dialog_find_string(wad_item, 0, "DOOM1", 1);
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
        user32_dialog_set_item_text(hwnd, 0x436, "0");
        DEBUG_LEVEL(1, "user32: Doom95 launcher autostart seed base wad DOOM1.WAD");
    }

    DEBUG_LEVEL(1, "user32: Doom95 launcher autostart click start");
    ((DLGPROC_WINE)lpDialogFunc)(hwnd, WM_COMMAND, (WPARAM)0x3f1u,
                                 (LPARAM)(uintptr_t)start_hwnd);

    /*
     * The launcher is modeless in our current USER32 stub path, so clicking
     * Start may leave the dialog alive instead of exiting it through the
     * normal modal lifecycle. If that happens, explicitly destroy it so the
     * game window can take over cleanly.
     */
    if (hwnd != 0) {
        DEBUG_LEVEL(1, "user32: Doom95 launcher autostart destroy dialog hwnd=0x%lx",
                    (unsigned long)(uintptr_t)hwnd);
        DestroyWindow(hwnd);
    }
}
