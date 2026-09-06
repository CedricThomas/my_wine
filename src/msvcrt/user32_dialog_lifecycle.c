/*
 * user32_dialog_lifecycle.c
 *
 * Dialog modal completion and teardown split out from the broader dialog
 * entrypoint cluster.
 */

#include <stdint.h>

#include "user32_dialog_priv.h"
#include "include/common.h"

extern BOOL KERNEL32_ABI DestroyWindow(HWND hWnd);

BOOL user32_dialog_end(HWND hDlg, intptr_t nResult)
{
    DEBUG_LEVEL(1, "user32: EndDialog hwnd=0x%lx result=%ld",
                (unsigned long)(uintptr_t)hDlg, (long)nResult);
    user32_dialog_mark_modal_end(hDlg, nResult);
    if (hDlg != 0)
        DestroyWindow(hDlg);
    return TRUE;
}

int user32_dialog_run_modal(HWND hwnd, void *lpDialogFunc)
{
    intptr_t result = 0;
    int ended = 0;

    if (!user32_dialog_run_modal_lifecycle(hwnd, lpDialogFunc, &result, &ended))
        return 0;
    if (!ended)
        user32_dialog_end(hwnd, 0);
    else
        user32_dialog_mark_modal_end(hwnd, result);
    DEBUG_LEVEL(1, "user32: DialogBoxParamA hwnd=0x%lx result=%ld ended=%d",
                (unsigned long)(uintptr_t)hwnd, (long)result, ended);
    user32_dialog_clear_modal(hwnd);
    return (int)result;
}
