/*
 * user32_dialog_modal.c
 *
 * Dialog class registration and modal state tracking split out from the
 * broader dialog entrypoint file.
 */

#include <stdint.h>

#include "user32_dialog_priv.h"
#include "include/common.h"

extern LRESULT KERNEL32_ABI DefWindowProcA(HWND hWnd, UINT Msg, WPARAM wParam, LPARAM lParam);
extern ATOM KERNEL32_ABI RegisterClassA(const WNDCLASSA *lpWndClass);

typedef struct {
    HWND dialog;
    void *dlgproc;
    intptr_t result;
    int ended;
} dialog_modal_state;

static int g_dialog_class_registered = 0;
static dialog_modal_state g_dialog_modals[16];

void user32_dialog_ensure_class(void)
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

void user32_dialog_set_modal_dlgproc(HWND dialog, void *dlgproc)
{
    dialog_modal_state *modal = dialog_find_modal(dialog, 1);

    if (modal)
        modal->dlgproc = dlgproc;
}

DLGPROC_WINE user32_dialog_get_modal_dlgproc(HWND dialog)
{
    dialog_modal_state *modal = dialog_find_modal(dialog, 0);

    if (!modal || !modal->dlgproc)
        return NULL;
    return (DLGPROC_WINE)modal->dlgproc;
}

BOOL user32_dialog_mark_modal_end(HWND hDlg, intptr_t nResult)
{
    dialog_modal_state *modal = dialog_find_modal(hDlg, 0);

    if (!modal)
        return FALSE;

    modal->ended = 1;
    modal->result = nResult;
    return TRUE;
}

int user32_dialog_run_modal_lifecycle(HWND hwnd, void *lpDialogFunc,
                                      intptr_t *result_out, int *ended_out)
{
    dialog_modal_state *modal = dialog_find_modal(hwnd, 0);

    if (!hwnd)
        return FALSE;
    if (modal == NULL)
        modal = dialog_find_modal(hwnd, 1);
    if (modal == NULL)
        return FALSE;

    /*
     * Minimal modal behavior: after WM_INITDIALOG, drive the default OK path
     * so guest dialog procedures can perform their normal save/apply work
     * before calling EndDialog().
     */
    if (!modal->ended && lpDialogFunc)
        ((DLGPROC_WINE)lpDialogFunc)(hwnd, WM_COMMAND, (WPARAM)IDOK, 0);
    if (!modal->ended && lpDialogFunc)
        ((DLGPROC_WINE)lpDialogFunc)(hwnd, WM_CLOSE, 0, 0);

    if (result_out)
        *result_out = modal->result;
    if (ended_out)
        *ended_out = modal->ended;
    return TRUE;
}

void user32_dialog_clear_modal(HWND dialog)
{
    dialog_modal_state *modal = dialog_find_modal(dialog, 0);

    if (modal)
        user32_memset(modal, 0, sizeof(*modal));
}
