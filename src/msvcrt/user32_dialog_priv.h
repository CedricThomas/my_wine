#ifndef MY_WINE_USER32_DIALOG_PRIV_H
#define MY_WINE_USER32_DIALOG_PRIV_H

#include "user32_priv.h"

typedef intptr_t (KERNEL32_ABI *DLGPROC_WINE)(HWND, UINT, WPARAM, LPARAM);

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

dialog_item_state *user32_dialog_find_item(HWND dialog, uint32_t id, int create);
dialog_item_state *user32_dialog_find_item_by_handle(HWND handle);
int user32_dialog_find_string(dialog_item_state *item, uint32_t start_idx,
                              const char *needle, int exact);
int user32_dialog_find_item_data_index(dialog_item_state *item, intptr_t needle);
void user32_dialog_try_doom95_autostart(HINSTANCE hInstance, HWND hwnd,
                                        const char *lpTemplateName, void *lpDialogFunc);

#endif /* MY_WINE_USER32_DIALOG_PRIV_H */
