/*
 * user32_dialog_message.c
 *
 * Dialog message routing split out from the core dialog creation/modal
 * entrypoint file.
 */

#include <stdint.h>

#include "user32_dialog_priv.h"

BOOL user32_dialog_dispatch_message(HWND hDlg, MSG *lpMsg)
{
    DLGPROC_WINE dlgproc;
    dialog_item_state *item;

    if (!hDlg || !lpMsg)
        return FALSE;

    dlgproc = user32_dialog_get_modal_dlgproc(hDlg);
    if (!dlgproc)
        return FALSE;

    if (lpMsg->hwnd != hDlg) {
        item = user32_dialog_find_item_by_handle(lpMsg->hwnd);
        if (!item || item->dialog != hDlg)
            return FALSE;
    }

    if (dlgproc(hDlg, lpMsg->message, lpMsg->wParam, lpMsg->lParam))
        return TRUE;
    return FALSE;
}

KERNEL32_STUB
BOOL IsDialogMessageA(HWND hDlg, MSG *lpMsg)
{
    return user32_dialog_dispatch_message(hDlg, lpMsg);
}
