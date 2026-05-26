/*
 * rb_event_state.c
 *
 * Owns backend event state: active window tracking, guest/backend route
 * tables, and the synthetic message queue shared by event translation paths.
 */

#include "rb_sdl2_priv.h"
#include <string.h>

typedef struct {
    uintptr_t hwnd;
    rb_window_t backend_win;
    uint32_t sdl_window_id;
    uintptr_t native_window_id;
} rb_window_route;

static uintptr_t g_active_window = 0;
static rb_window_route *g_window_routes = NULL;
static size_t g_window_route_capacity = 0;
static rb_msg_t *g_synthetic_queue = NULL;
static size_t g_synthetic_queue_capacity = 0;
static size_t g_synthetic_queue_count = 0;

static rb_window *rb_event_get_backend_window(rb_window_t win)
{
    if (wine_handle_get_type((uint32_t)win) != HANDLE_TYPE_RB_WINDOW)
        return NULL;
    return (rb_window *)wine_handle_get((uint32_t)win);
}

static int rb_event_ensure_route_capacity(size_t needed)
{
    size_t new_capacity;
    rb_window_route *new_routes;

    if (needed <= g_window_route_capacity)
        return RB_OK;

    new_capacity = g_window_route_capacity ? g_window_route_capacity * 2 : 16;
    while (new_capacity < needed)
        new_capacity *= 2;

    new_routes = realloc(g_window_routes, new_capacity * sizeof(*new_routes));
    if (!new_routes)
        return RB_FAIL;

    memset(new_routes + g_window_route_capacity, 0,
           (new_capacity - g_window_route_capacity) * sizeof(*new_routes));
    g_window_routes = new_routes;
    g_window_route_capacity = new_capacity;
    return RB_OK;
}

static int rb_event_ensure_synthetic_capacity(size_t needed)
{
    size_t new_capacity;
    rb_msg_t *new_queue;

    if (needed <= g_synthetic_queue_capacity)
        return RB_OK;

    new_capacity = g_synthetic_queue_capacity ? g_synthetic_queue_capacity * 2 : 8;
    while (new_capacity < needed)
        new_capacity *= 2;

    new_queue = realloc(g_synthetic_queue, new_capacity * sizeof(*new_queue));
    if (!new_queue)
        return RB_FAIL;

    g_synthetic_queue = new_queue;
    g_synthetic_queue_capacity = new_capacity;
    return RB_OK;
}

static int rb_event_find_route_by_hwnd(uintptr_t hwnd)
{
    size_t i;
    for (i = 0; i < g_window_route_capacity; i++) {
        if (g_window_routes[i].hwnd == hwnd)
            return (int)i;
    }
    return -1;
}

static int rb_event_find_route_by_sdl_window(uint32_t window_id)
{
    size_t i;
    if (!window_id)
        return -1;

    for (i = 0; i < g_window_route_capacity; i++) {
        if (g_window_routes[i].hwnd != 0 &&
            g_window_routes[i].sdl_window_id == window_id)
            return (int)i;
    }
    return -1;
}

static int rb_event_find_route_by_native_window(uintptr_t native_window_id)
{
    size_t i;
    if (!native_window_id)
        return -1;

    for (i = 0; i < g_window_route_capacity; i++) {
        if (g_window_routes[i].hwnd != 0 &&
            g_window_routes[i].native_window_id == native_window_id)
            return (int)i;
    }
    return -1;
}

static int rb_event_alloc_route_slot(void)
{
    size_t i;

    if (rb_event_ensure_route_capacity(g_window_route_capacity + 1) != RB_OK)
        return -1;

    for (i = 0; i < g_window_route_capacity; i++) {
        if (g_window_routes[i].hwnd == 0)
            return (int)i;
    }
    return -1;
}

void rb_event_set_active_window(uintptr_t hwnd)
{
    g_active_window = hwnd;
}

uintptr_t rb_event_get_active_window(void)
{
    return g_active_window;
}

int rb_event_bind_window(uintptr_t hwnd, rb_window_t win)
{
    rb_window *wnd = rb_event_get_backend_window(win);
    int idx;

    if (!hwnd || !wnd || !wnd->sdl_window_id)
        return RB_FAIL;

    idx = rb_event_find_route_by_hwnd(hwnd);
    if (idx < 0)
        idx = rb_event_alloc_route_slot();
    if (idx < 0)
        return RB_FAIL;

    g_window_routes[idx].hwnd = hwnd;
    g_window_routes[idx].backend_win = win;
    g_window_routes[idx].sdl_window_id = wnd->sdl_window_id;
    g_window_routes[idx].native_window_id = wnd->native_window_id;
    return RB_OK;
}

void rb_event_unbind_window(uintptr_t hwnd)
{
    int idx = rb_event_find_route_by_hwnd(hwnd);
    if (idx < 0)
        return;

    memset(&g_window_routes[idx], 0, sizeof(g_window_routes[idx]));
    if (g_active_window == hwnd)
        g_active_window = 0;
}

uint32_t rb_event_get_sdl_window_id(uintptr_t hwnd)
{
    int idx = rb_event_find_route_by_hwnd(hwnd);
    return idx >= 0 ? g_window_routes[idx].sdl_window_id : 0;
}

uintptr_t rb_event_resolve_hwnd_from_sdl_window(uint32_t window_id)
{
    int idx = rb_event_find_route_by_sdl_window(window_id);
    return idx >= 0 ? g_window_routes[idx].hwnd : 0;
}

uintptr_t rb_event_resolve_hwnd_from_native_window(uintptr_t native_window_id)
{
    int idx = rb_event_find_route_by_native_window(native_window_id);
    return idx >= 0 ? g_window_routes[idx].hwnd : 0;
}

rb_window *rb_event_resolve_backend_window(uintptr_t hwnd)
{
    int idx = rb_event_find_route_by_hwnd(hwnd);

    if (idx < 0)
        return NULL;
    return rb_event_get_backend_window(g_window_routes[idx].backend_win);
}

size_t rb_event_window_route_capacity(void)
{
    return g_window_route_capacity;
}

uintptr_t rb_event_window_route_hwnd_at(size_t idx)
{
    if (idx >= g_window_route_capacity)
        return 0;
    return g_window_routes[idx].hwnd;
}

int rb_event_push_synthetic(const rb_msg_t *msg)
{
    if (!msg)
        return RB_FAIL;
    if (rb_event_ensure_synthetic_capacity(g_synthetic_queue_count + 1) != RB_OK)
        return RB_FAIL;

    g_synthetic_queue[g_synthetic_queue_count++] = *msg;
    return RB_OK;
}

int rb_event_pop_synthetic(rb_msg_t *msg)
{
    size_t i;

    if (!msg || g_synthetic_queue_count == 0)
        return 0;

    *msg = g_synthetic_queue[0];
    for (i = 1; i < g_synthetic_queue_count; i++)
        g_synthetic_queue[i - 1] = g_synthetic_queue[i];
    g_synthetic_queue_count--;
    return 1;
}
