/*
 * user32_dialog_controls.c
 *
 * Dialog control message handling split out from the broader dialog/modal
 * entrypoint file.
 */

#include <stdio.h>

#include "user32_dialog_priv.h"
#include "include/common.h"

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
    } else if ((uint32_t)item->cur_sel >= idx) {
        item->cur_sel++;
    }

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

    if (item->item_count == 0) {
        item->cur_sel = -1;
    } else if (item->cur_sel == (int32_t)idx) {
        item->cur_sel = -1;
    } else if (item->cur_sel > (int32_t)idx) {
        item->cur_sel--;
    }

    if (item->cur_sel >= 0 && (uint32_t)item->cur_sel < item->item_count) {
        user32_strncpy(item->text, item->items[item->cur_sel], sizeof(item->text) - 1);
        item->text[sizeof(item->text) - 1] = '\0';
    } else {
        item->text[0] = '\0';
    }

    return (int)item->item_count;
}

LRESULT user32_dialog_send_control_message(HWND hWnd, UINT Msg, WPARAM wParam, LPARAM lParam)
{
    dialog_item_state *item = user32_dialog_find_item_by_handle(hWnd);

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
        int idx = user32_dialog_find_string(item, (uint32_t)wParam,
                                            (const char *)(uintptr_t)lParam, 0);

        DEBUG_LEVEL(2, "user32: dialog ctrl id=0x%x find start=%ld text='%s' -> %d",
                    (unsigned)item->id, (long)(int32_t)wParam,
                    (const char *)(uintptr_t)lParam ? (const char *)(uintptr_t)lParam : "",
                    idx);
        return (LRESULT)idx;
    }

    case LB_FINDSTRINGEXACT: {
        int idx = user32_dialog_find_string(item, (uint32_t)wParam,
                                            (const char *)(uintptr_t)lParam, 1);

        DEBUG_LEVEL(2, "user32: dialog ctrl id=0x%x find exact start=%ld text='%s' -> %d",
                    (unsigned)item->id, (long)(int32_t)wParam,
                    (const char *)(uintptr_t)lParam ? (const char *)(uintptr_t)lParam : "",
                    idx);
        return (LRESULT)idx;
    }

    case CB_SELECTSTRING:
    case LB_SELECTSTRING: {
        int idx = user32_dialog_find_string(item, (uint32_t)wParam,
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
            dialog_item_state *buddy = user32_dialog_find_item_by_handle(item->buddy);

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
