/*
 * rb_event_shutdown.c
 *
 * Owns shutdown sequencing for the SDL backend: queueing guest windows for
 * close delivery and emitting the final WM_QUIT when required.
 */

#include "rb_sdl2_priv.h"
#include "include/debug.h"
#include <string.h>

#define WM_CLOSE          0x0010
#define WM_QUIT           0x0012

static uintptr_t *g_shutdown_hwnds = NULL;
static size_t g_shutdown_hwnd_capacity = 0;
static size_t g_shutdown_hwnd_count = 0;
static size_t g_shutdown_hwnd_head = 0;
static int g_shutdown_force_quit_pending = 0;

static int rb_event_ensure_shutdown_capacity(size_t needed)
{
    size_t new_capacity;
    uintptr_t *new_hwnds;

    if (needed <= g_shutdown_hwnd_capacity)
        return RB_OK;

    new_capacity = g_shutdown_hwnd_capacity ? g_shutdown_hwnd_capacity * 2 : 16;
    while (new_capacity < needed)
        new_capacity *= 2;

    new_hwnds = realloc(g_shutdown_hwnds, new_capacity * sizeof(*new_hwnds));
    if (!new_hwnds)
        return RB_FAIL;

    g_shutdown_hwnds = new_hwnds;
    g_shutdown_hwnd_capacity = new_capacity;
    return RB_OK;
}

void rb_event_begin_shutdown(void)
{
    size_t count = 0;
    size_t idx;

    g_shutdown_hwnd_head = 0;
    g_shutdown_hwnd_count = 0;
    g_shutdown_force_quit_pending = 0;

    if (rb_event_ensure_shutdown_capacity(rb_event_window_route_capacity() + 1) != RB_OK)
        return;

    if (rb_event_get_active_window() != 0 &&
        rb_event_resolve_backend_window(rb_event_get_active_window()) != NULL) {
        g_shutdown_hwnds[count++] = rb_event_get_active_window();
    }

    for (idx = 0; idx < rb_event_window_route_capacity(); idx++) {
        uintptr_t hwnd = rb_event_window_route_hwnd_at(idx);

        if (hwnd == 0 || hwnd == rb_event_get_active_window())
            continue;
        g_shutdown_hwnds[count++] = hwnd;
    }

    g_shutdown_hwnd_count = count;
    DEBUG_LEVEL(1, "rb_event: begin shutdown queued=%lu active=0x%lx force_quit=%d",
                (unsigned long)g_shutdown_hwnd_count,
                (unsigned long)rb_event_get_active_window(),
                g_shutdown_force_quit_pending);
}

int rb_event_translate_shutdown(rb_msg_t *out_msg)
{
    while (g_shutdown_hwnd_head < g_shutdown_hwnd_count) {
        uintptr_t hwnd = g_shutdown_hwnds[g_shutdown_hwnd_head++];

        if (hwnd == 0 || rb_event_resolve_backend_window(hwnd) == NULL)
            continue;

        memset(out_msg, 0, sizeof(*out_msg));
        out_msg->hwnd = hwnd;
        out_msg->message = WM_CLOSE;
        DEBUG_LEVEL(1, "rb_event: shutdown emit WM_CLOSE hwnd=0x%lx",
                    (unsigned long)hwnd);
        return 1;
    }

    if (g_shutdown_force_quit_pending) {
        memset(out_msg, 0, sizeof(*out_msg));
        out_msg->message = WM_QUIT;
        g_shutdown_force_quit_pending = 0;
        DEBUG_LEVEL(1, "rb_event: shutdown emit WM_QUIT");
        return 1;
    }

    return 0;
}
