/*
 * user32_message_queue.c
 *
 * Queue/filter/quit helpers shared by the USER32 message-loop exports.
 */

#include <stddef.h>

#include "user32_message_priv.h"
#include "include/debug.h"
#include "../include/render_backend.h"

static int g_quit_pending = 0;
static int g_quit_exit_code = 0;

#define USER32_TRANSLATED_QUEUE_CAPACITY 64
#define USER32_POSTED_QUEUE_CAPACITY 64

static rb_msg_t g_translated_queue[USER32_TRANSLATED_QUEUE_CAPACITY];
static size_t g_translated_queue_head = 0;
static size_t g_translated_queue_count = 0;
static rb_msg_t g_posted_queue[USER32_POSTED_QUEUE_CAPACITY];
static size_t g_posted_queue_head = 0;
static size_t g_posted_queue_count = 0;

static int user32_queue_push(rb_msg_t *queue, size_t capacity,
                             size_t *head, size_t *count,
                             const rb_msg_t *msg)
{
    size_t tail;

    if (!queue || !head || !count || !msg || *count >= capacity)
        return 0;

    tail = (*head + *count) % capacity;
    queue[tail] = *msg;
    (*count)++;
    return 1;
}

static int user32_posted_queue_push(const rb_msg_t *msg)
{
    return user32_queue_push(g_posted_queue, USER32_POSTED_QUEUE_CAPACITY,
                             &g_posted_queue_head, &g_posted_queue_count, msg);
}

static int user32_translated_queue_push(const rb_msg_t *msg)
{
    return user32_queue_push(g_translated_queue, USER32_TRANSLATED_QUEUE_CAPACITY,
                             &g_translated_queue_head, &g_translated_queue_count, msg);
}

static int user32_message_target_alive(const rb_msg_t *msg)
{
    if (!msg)
        return 0;

    if (msg->message == WM_QUIT || msg->hwnd == 0)
        return 1;

    return get_window_entry((HWND)msg->hwnd) != NULL;
}

static int user32_message_matches_filter(const rb_msg_t *msg, HWND hwnd_filter,
                                         UINT min_filter, UINT max_filter)
{
    UINT message;

    if (!msg)
        return 0;

    message = msg->message;
    if (message == WM_QUIT)
        return 1;

    if (user32_is_keyboard_message(message))
        hwnd_filter = 0;

    if (hwnd_filter != 0 && get_window_entry(hwnd_filter) == NULL)
        hwnd_filter = 0;

    if (hwnd_filter != 0 && msg->hwnd != (uintptr_t)hwnd_filter)
        return 0;

    if (min_filter == 0 && max_filter == 0)
        return 1;

    if (max_filter == 0)
        return message >= min_filter;

    return message >= min_filter && message <= max_filter;
}

int user32_synthesize_quit_message(int remove, HWND hwnd_filter,
                                   UINT min_filter, UINT max_filter,
                                   rb_msg_t *msg)
{
    if (!msg)
        return 0;

    if (g_quit_pending) {
        user32_memset(msg, 0, sizeof(*msg));
        msg->message = WM_QUIT;
        msg->wParam = (WPARAM)g_quit_exit_code;
        if (!user32_message_matches_filter(msg, hwnd_filter, min_filter, max_filter))
            return 0;
        if (remove)
            g_quit_pending = 0;
        return 1;
    }

    return 0;
}

void user32_copy_rb_msg_to_msg(const rb_msg_t *src, MSG *dst)
{
    dst->hwnd = src->hwnd;
    dst->message = src->message;
    dst->wParam = src->wParam;
    dst->lParam = src->lParam;
    dst->time = src->time;
    dst->pt.x = src->pt_x;
    dst->pt.y = src->pt_y;
}

static int user32_queue_find_matching(const rb_msg_t *queue, size_t capacity,
                                      size_t head, size_t count,
                                      HWND hwnd_filter, UINT min_filter,
                                      UINT max_filter)
{
    size_t i;

    for (i = 0; i < count; i++) {
        size_t idx = (head + i) % capacity;
        if (user32_message_matches_filter(&queue[idx], hwnd_filter,
                                          min_filter, max_filter)) {
            return (int)idx;
        }
    }

    return -1;
}

static int user32_queue_take_at(rb_msg_t *queue, size_t capacity,
                                size_t *head, size_t *count,
                                int match_idx, int remove, rb_msg_t *msg)
{
    size_t idx;
    size_t i;

    if (!queue || !head || !count || !msg || *count == 0 || match_idx < 0)
        return 0;

    idx = (size_t)match_idx;
    *msg = queue[idx];
    if (!remove)
        return 1;

    for (i = idx; i != ((*head + *count - 1) % capacity); i = (i + 1) % capacity) {
        size_t next = (i + 1) % capacity;
        queue[i] = queue[next];
    }
    (*count)--;
    return 1;
}

static void user32_queue_discard_dead_targets(rb_msg_t *queue, size_t capacity,
                                              size_t *head, size_t *count)
{
    size_t old_count;
    size_t old_head;
    size_t write_count;
    size_t i;

    if (!queue || !head || !count || *count == 0)
        return;

    old_head = *head;
    old_count = *count;
    write_count = 0;

    for (i = 0; i < old_count; i++) {
        rb_msg_t msg = queue[(old_head + i) % capacity];

        if (!user32_message_target_alive(&msg))
            continue;

        queue[(old_head + write_count) % capacity] = msg;
        write_count++;
    }

    *count = write_count;
}

static int user32_posted_queue_take_matching(int remove, HWND hwnd_filter,
                                             UINT min_filter, UINT max_filter,
                                             rb_msg_t *msg)
{
    user32_queue_discard_dead_targets(g_posted_queue, USER32_POSTED_QUEUE_CAPACITY,
                                      &g_posted_queue_head, &g_posted_queue_count);

    return user32_queue_take_at(g_posted_queue, USER32_POSTED_QUEUE_CAPACITY,
                                &g_posted_queue_head, &g_posted_queue_count,
                                user32_queue_find_matching(g_posted_queue,
                                                           USER32_POSTED_QUEUE_CAPACITY,
                                                           g_posted_queue_head,
                                                           g_posted_queue_count,
                                                           hwnd_filter,
                                                           min_filter,
                                                           max_filter),
                                remove, msg);
}

static int user32_translated_queue_take_matching(int remove, HWND hwnd_filter,
                                                 UINT min_filter, UINT max_filter,
                                                 rb_msg_t *msg)
{
    user32_queue_discard_dead_targets(g_translated_queue,
                                      USER32_TRANSLATED_QUEUE_CAPACITY,
                                      &g_translated_queue_head,
                                      &g_translated_queue_count);

    return user32_queue_take_at(g_translated_queue,
                                USER32_TRANSLATED_QUEUE_CAPACITY,
                                &g_translated_queue_head,
                                &g_translated_queue_count,
                                user32_queue_find_matching(g_translated_queue,
                                                           USER32_TRANSLATED_QUEUE_CAPACITY,
                                                           g_translated_queue_head,
                                                           g_translated_queue_count,
                                                           hwnd_filter,
                                                           min_filter,
                                                           max_filter),
                                remove, msg);
}

int user32_fetch_queued_message(int blocking, int remove,
                                HWND hwnd_filter,
                                UINT min_filter, UINT max_filter,
                                rb_msg_t *msg)
{
    rb_msg_t incoming;
    int ret;

    if (!msg)
        return 0;

    if (user32_posted_queue_take_matching(remove, hwnd_filter, min_filter, max_filter, msg))
        return 1;

    if (user32_translated_queue_take_matching(remove, hwnd_filter, min_filter, max_filter, msg))
        return 1;

    for (;;) {
        user32_memset(&incoming, 0, sizeof(incoming));
        if (blocking) {
            ret = rb_event_wait(&incoming);
            if (ret < 0 || incoming.message == 0)
                return 0;
        } else {
            ret = rb_event_peek(&incoming);
            if (ret <= 0 || incoming.message == 0)
                return 0;
        }

        if (!user32_message_target_alive(&incoming))
            continue;

        if (user32_message_matches_filter(&incoming, hwnd_filter, min_filter, max_filter)) {
            if (!remove) {
                if (!user32_translated_queue_push(&incoming))
                    return 0;
            }
            *msg = incoming;
            return 1;
        }

        if (!user32_translated_queue_push(&incoming))
            return 0;
    }
}

KERNEL32_STUB
BOOL PostMessageA(HWND hWnd, UINT Msg, WPARAM wParam, LPARAM lParam)
{
    rb_msg_t rb;

    if (hWnd != 0 && get_window_entry(hWnd) == NULL)
        return FALSE;

    user32_memset(&rb, 0, sizeof(rb));
    rb.hwnd = (uintptr_t)hWnd;
    rb.message = Msg;
    rb.wParam = wParam;
    rb.lParam = lParam;

    return user32_posted_queue_push(&rb) ? TRUE : FALSE;
}

KERNEL32_STUB
void PostQuitMessage(int nExitCode)
{
    g_quit_pending = 1;
    g_quit_exit_code = nExitCode;
    DEBUG_WRITE_ERR("user32: PostQuitMessage\n",
                    sizeof("user32: PostQuitMessage\n") - 1);
}

KERNEL32_STUB
BOOL SystemParametersInfoA(UINT uiAction, UINT uiParam, void *pvParam, UINT fWinIni)
{
    (void)uiAction;
    (void)uiParam;
    (void)pvParam;
    (void)fWinIni;
    return TRUE;
}
