/*
 * user32_dialog_items.c
 *
 * Dialog item lookup and state mutation exports split out from the core
 * dialog creation/modal entrypoint file.
 */

#include <stdint.h>

#include "user32_dialog_priv.h"
#include "include/common.h"

HWND user32_dialog_get_item_handle(HWND hDlg, int nIDDlgItem)
{
    dialog_item_state *item = user32_dialog_find_item(hDlg, (uint32_t)nIDDlgItem, 1);
    if (!item)
        return 0;
    DEBUG_LEVEL(2, "user32: GetDlgItem dialog=0x%lx id=0x%x -> handle=0x%lx",
                (unsigned long)(uintptr_t)hDlg, (unsigned)nIDDlgItem,
                (unsigned long)(uintptr_t)item->handle);
    return item->handle;
}

BOOL user32_dialog_set_item_text(HWND hDlg, int nIDDlgItem, const char *lpString)
{
    dialog_item_state *item = user32_dialog_find_item(hDlg, (uint32_t)nIDDlgItem, 1);
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

KERNEL32_STUB
HWND GetDlgItem(HWND hDlg, int nIDDlgItem)
{
    return user32_dialog_get_item_handle(hDlg, nIDDlgItem);
}

KERNEL32_STUB
BOOL CheckDlgButton(HWND hDlg, int nIDButton, UINT uCheck)
{
    dialog_item_state *item = user32_dialog_find_item(hDlg, (uint32_t)nIDButton, 1);
    if (!item)
        return FALSE;
    item->state = uCheck;
    return TRUE;
}

KERNEL32_STUB
UINT IsDlgButtonChecked(HWND hDlg, int nIDButton)
{
    dialog_item_state *item = user32_dialog_find_item(hDlg, (uint32_t)nIDButton, 0);
    return item ? item->state : 0;
}

KERNEL32_STUB
BOOL SetDlgItemTextA(HWND hDlg, int nIDDlgItem, const char *lpString)
{
    return user32_dialog_set_item_text(hDlg, nIDDlgItem, lpString);
}
