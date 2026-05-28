/*
 * user32_dialog_state.c
 *
 * Dialog item state ownership and handle-backed metadata helpers split out
 * from the dialog entrypoint file.
 */

#include <stdint.h>
#include <string.h>

#include "user32_dialog_priv.h"

static dialog_item_state g_dialog_items[64];
static uint32_t g_next_dialog_item_handle = 0x40000000u;

int user32_dialog_find_string(dialog_item_state *item, uint32_t start_idx,
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

dialog_item_state *user32_dialog_find_item(HWND dialog, uint32_t id, int create)
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

dialog_item_state *user32_dialog_find_item_by_handle(HWND handle)
{
    int i;

    for (i = 0; i < 64; i++) {
        if (g_dialog_items[i].handle == handle)
            return &g_dialog_items[i];
    }
    return NULL;
}

int user32_dialog_find_item_data_index(dialog_item_state *item, intptr_t needle)
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

LONG_PTR user32_dialog_get_window_long_ptr(HWND hWnd, int nIndex)
{
    dialog_item_state *item = user32_dialog_find_item_by_handle(hWnd);

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
