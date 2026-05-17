/*
 * rb_event.c
 *
 * SDL2 backend — event system.
 * Translates SDL events to Windows MSG-compatible rb_msg_t and vice versa.
 */

#include "rb_sdl2_priv.h"
#include "include/debug.h"
#include <stdlib.h>
#include <string.h>

/* ---- Active window tracking ---- */

static uintptr_t g_active_window = 0;
static int g_alt_key_down = 0;
#define RB_MAX_WINDOW_ROUTES 64

typedef struct {
    uintptr_t hwnd;
    uint32_t sdl_window_id;
    uintptr_t native_window_id;
} rb_window_route;

static rb_window_route g_window_routes[RB_MAX_WINDOW_ROUTES];

void rb_event_set_active_window(uintptr_t hwnd)
{
    g_active_window = hwnd;
}

uintptr_t rb_event_get_active_window(void)
{
    return g_active_window;
}

static rb_window *rb_event_get_backend_window(rb_window_t win)
{
    if (wine_handle_get_type((uint32_t)win) != HANDLE_TYPE_RB_WINDOW)
        return NULL;
    return (rb_window *)wine_handle_get((uint32_t)win);
}

static int rb_event_find_route_by_hwnd(uintptr_t hwnd)
{
    int i;
    for (i = 0; i < RB_MAX_WINDOW_ROUTES; i++) {
        if (g_window_routes[i].hwnd == hwnd)
            return i;
    }
    return -1;
}

static int rb_event_find_route_by_sdl_window(uint32_t window_id)
{
    int i;
    if (!window_id)
        return -1;

    for (i = 0; i < RB_MAX_WINDOW_ROUTES; i++) {
        if (g_window_routes[i].hwnd != 0 &&
            g_window_routes[i].sdl_window_id == window_id)
            return i;
    }
    return -1;
}

static int rb_event_find_route_by_native_window(uintptr_t native_window_id)
{
    int i;
    if (!native_window_id)
        return -1;

    for (i = 0; i < RB_MAX_WINDOW_ROUTES; i++) {
        if (g_window_routes[i].hwnd != 0 &&
            g_window_routes[i].native_window_id == native_window_id)
            return i;
    }
    return -1;
}

static int rb_event_alloc_route_slot(void)
{
    int i;
    for (i = 0; i < RB_MAX_WINDOW_ROUTES; i++) {
        if (g_window_routes[i].hwnd == 0)
            return i;
    }
    return -1;
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

static uintptr_t rb_event_resolve_hwnd_from_sdl_window(uint32_t window_id)
{
    int idx = rb_event_find_route_by_sdl_window(window_id);
    return idx >= 0 ? g_window_routes[idx].hwnd : 0;
}

static uintptr_t rb_event_resolve_hwnd_from_native_window(uintptr_t native_window_id)
{
    int idx = rb_event_find_route_by_native_window(native_window_id);
    return idx >= 0 ? g_window_routes[idx].hwnd : 0;
}

/* ---- Windows message constants ---- */

#define WM_CREATE         0x0001
#define WM_MOVE           0x0003
#define WM_SIZE           0x0005
#define WM_CLOSE          0x0010
#define WM_QUIT           0x0012
#define WM_KEYDOWN        0x0100
#define WM_KEYUP          0x0101
#define WM_CHAR           0x0102
#define WM_SYSKEYDOWN     0x0104
#define WM_SYSKEYUP       0x0105
#define WM_SYSCOMMAND     0x0112
#define WM_MOUSEMOVE      0x0200
#define WM_LBUTTONDOWN    0x0201
#define WM_LBUTTONUP      0x0202
#define WM_LBUTTONDBLCLK  0x0203
#define WM_RBUTTONDOWN    0x0204
#define WM_RBUTTONUP      0x0205
#define WM_RBUTTONDBLCLK  0x0206

#define VK_BACK           0x08
#define VK_TAB            0x09
#define VK_RETURN         0x0D
#define VK_SHIFT          0x10
#define VK_CONTROL        0x11
#define VK_MENU           0x12
#define VK_ESCAPE         0x1B
#define VK_SPACE          0x20
#define VK_PRIOR          0x21
#define VK_NEXT           0x22
#define VK_END            0x23
#define VK_HOME           0x24
#define VK_LEFT           0x25
#define VK_UP             0x26
#define VK_RIGHT          0x27
#define VK_DOWN           0x28
#define VK_INSERT         0x2D
#define VK_DELETE         0x2E
#define VK_F1             0x70
#define VK_F2             0x71
#define VK_F3             0x72
#define VK_F4             0x73
#define VK_F5             0x74
#define VK_F6             0x75
#define VK_F7             0x76
#define VK_F8             0x77
#define VK_F9             0x78
#define VK_F10            0x79
#define VK_F11            0x7A
#define VK_F12            0x7B
#define VK_LSHIFT         0xA0
#define VK_RSHIFT         0xA1
#define VK_LCONTROL       0xA2
#define VK_RCONTROL       0xA3
#define VK_LMENU          0xA4
#define VK_RMENU          0xA5

#define SC_MINIMIZE       0xF020
#define SC_CLOSE          0xF060
#define SC_RESTORE        0xF120

static uintptr_t g_shutdown_hwnds[RB_MAX_WINDOW_ROUTES];
static size_t g_shutdown_hwnd_count = 0;
static size_t g_shutdown_hwnd_head = 0;
static int g_shutdown_force_quit_pending = 0;

/* ---- SDL 2.0.18+ compatibility: 5-arg SDL_PeepEvents ---- */

static inline int rb_peep_events(SDL_Event *ev, int n, SDL_eventaction action)
{
#if SDL_VERSION_ATLEAST(2, 0, 18)
    return SDL_PeepEvents(ev, n, action, 0, 0xFFFFFFFF);
#else
    return SDL_PeepEvents(ev, n, action, 0);
#endif
}

static uintptr_t rb_sdl_wait_event_call(void *arg)
{
    return (uintptr_t)SDL_WaitEventTimeout((SDL_Event *)arg, 100);
}

static uintptr_t rb_sdl_peep_event_call(void *arg)
{
    return (uintptr_t)rb_peep_events((SDL_Event *)arg, 1, SDL_GETEVENT);
}

static uintptr_t rb_sdl_push_event_call(void *arg)
{
    return (uintptr_t)SDL_PushEvent((SDL_Event *)arg);
}

static uintptr_t rb_sdl_repaint_window_black_call(void *arg)
{
    uint32_t window_id = *(uint32_t *)arg;
    SDL_Window *window = SDL_GetWindowFromID(window_id);
    if (!window)
        return 0;

    SDL_Surface *surface = SDL_GetWindowSurface(window);
    if (!surface)
        return 0;

    uint32_t color = SDL_MapRGB(surface->format, 0, 0, 0);
    SDL_FillRect(surface, NULL, color);
    SDL_UpdateWindowSurface(window);
    return 1;
}

static void repaint_active_window_black(uint32_t window_id)
{
    rb_call_on_host_stack(rb_sdl_repaint_window_black_call, &window_id);
}

static void rb_event_begin_shutdown(void)
{
    size_t count = 0;
    int idx;

    g_shutdown_hwnd_head = 0;
    g_shutdown_hwnd_count = 0;
    g_shutdown_force_quit_pending = 1;

    if (g_active_window != 0 &&
        rb_event_find_route_by_hwnd(g_active_window) >= 0) {
        g_shutdown_hwnds[count++] = g_active_window;
    }

    for (idx = 0; idx < RB_MAX_WINDOW_ROUTES; idx++) {
        uintptr_t hwnd = g_window_routes[idx].hwnd;

        if (hwnd == 0 || hwnd == g_active_window)
            continue;
        g_shutdown_hwnds[count++] = hwnd;
    }

    g_shutdown_hwnd_count = count;
    DEBUG_LEVEL(1, "rb_event: begin shutdown queued=%lu active=0x%lx",
                (unsigned long)g_shutdown_hwnd_count,
                (unsigned long)g_active_window);
}

static int rb_event_translate_shutdown(rb_msg_t *out_msg)
{
    while (g_shutdown_hwnd_head < g_shutdown_hwnd_count) {
        uintptr_t hwnd = g_shutdown_hwnds[g_shutdown_hwnd_head++];

        if (hwnd == 0 || rb_event_find_route_by_hwnd(hwnd) < 0)
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

static int rb_keycode_to_vk(SDL_Keycode sym, SDL_Scancode scancode)
{
    if (sym >= SDLK_a && sym <= SDLK_z)
        return 'A' + (int)(sym - SDLK_a);
    if (sym >= SDLK_0 && sym <= SDLK_9)
        return '0' + (int)(sym - SDLK_0);

    switch (sym) {
    case SDLK_BACKSPACE: return VK_BACK;
    case SDLK_TAB: return VK_TAB;
    case SDLK_RETURN: return VK_RETURN;
    case SDLK_ESCAPE: return VK_ESCAPE;
    case SDLK_SPACE: return VK_SPACE;
    case SDLK_PAGEUP: return VK_PRIOR;
    case SDLK_PAGEDOWN: return VK_NEXT;
    case SDLK_END: return VK_END;
    case SDLK_HOME: return VK_HOME;
    case SDLK_LEFT: return VK_LEFT;
    case SDLK_UP: return VK_UP;
    case SDLK_RIGHT: return VK_RIGHT;
    case SDLK_DOWN: return VK_DOWN;
    case SDLK_INSERT: return VK_INSERT;
    case SDLK_DELETE: return VK_DELETE;
    case SDLK_F1: return VK_F1;
    case SDLK_F2: return VK_F2;
    case SDLK_F3: return VK_F3;
    case SDLK_F4: return VK_F4;
    case SDLK_F5: return VK_F5;
    case SDLK_F6: return VK_F6;
    case SDLK_F7: return VK_F7;
    case SDLK_F8: return VK_F8;
    case SDLK_F9: return VK_F9;
    case SDLK_F10: return VK_F10;
    case SDLK_F11: return VK_F11;
    case SDLK_F12: return VK_F12;
    case SDLK_LSHIFT: return VK_LSHIFT;
    case SDLK_RSHIFT: return VK_RSHIFT;
    case SDLK_LCTRL: return VK_LCONTROL;
    case SDLK_RCTRL: return VK_RCONTROL;
    case SDLK_LALT: return VK_LMENU;
    case SDLK_RALT: return VK_RMENU;
    default:
        break;
    }

    return scancode_to_vk(scancode);
}

static int rb_is_system_key_event(const SDL_KeyboardEvent *key)
{
    SDL_Keycode sym = key->keysym.sym;

    if (sym == SDLK_LALT || sym == SDLK_RALT)
        return 1;
    if (g_alt_key_down)
        return 1;

    return (key->keysym.mod & KMOD_ALT) != 0;
}

static uint32_t rb_build_key_lparam(const SDL_KeyboardEvent *key, int is_keyup)
{
    uint32_t lparam = 1u | ((uint32_t)key->keysym.scancode << 16);

    if (rb_is_system_key_event(key))
        lparam |= (1u << 29);
    if (key->repeat || is_keyup)
        lparam |= (1u << 30);
    if (is_keyup)
        lparam |= (1u << 31);

    return lparam;
}

static uintptr_t rb_event_get_window_hwnd(SDL_Event *sdl)
{
    uint32_t window_id = 0;

    switch (sdl->type) {
    case SDL_KEYDOWN:
    case SDL_KEYUP:
        window_id = sdl->key.windowID;
        break;
    case SDL_TEXTINPUT:
        window_id = sdl->text.windowID;
        break;
    case SDL_MOUSEMOTION:
        window_id = sdl->motion.windowID;
        break;
    case SDL_MOUSEBUTTONDOWN:
    case SDL_MOUSEBUTTONUP:
        window_id = sdl->button.windowID;
        break;
    case SDL_MOUSEWHEEL:
        window_id = sdl->wheel.windowID;
        break;
    case SDL_WINDOWEVENT:
        window_id = sdl->window.windowID;
        break;
    default:
        break;
    }

    if (window_id)
        return rb_event_resolve_hwnd_from_sdl_window(window_id);
    return 0;
}

/* ---- translate_sdl_event ----
 * Returns 1 on successful translation, 0 for unknown/untranslatable events. */

static int translate_sdl_event(SDL_Event *sdl, rb_msg_t *msg)
{
    uintptr_t event_hwnd = rb_event_get_window_hwnd(sdl);

    memset(msg, 0, sizeof(*msg));
    msg->hwnd = event_hwnd;

    switch (sdl->type) {
    case SDL_KEYDOWN:
    {
        int vk;
        if (!msg->hwnd)
            msg->hwnd = g_active_window;
        if (!msg->hwnd)
            return 0;
        if (sdl->key.keysym.sym == SDLK_LALT || sdl->key.keysym.sym == SDLK_RALT)
            g_alt_key_down = 1;
        vk = rb_keycode_to_vk(sdl->key.keysym.sym, sdl->key.keysym.scancode);
        if (vk < 0)
            return 0;
        if (rb_is_system_key_event(&sdl->key) && vk == VK_F4) {
            msg->message = WM_CLOSE;
            msg->wParam = 0;
            msg->lParam = 0;
            msg->time = (uint32_t)sdl->key.timestamp;
            DEBUG_WRITE_ERR("rb_event: Alt+F4 -> WM_CLOSE\n",
                            sizeof("rb_event: Alt+F4 -> WM_CLOSE\n") - 1);
            DEBUG_LEVEL(1, "rb_event: Alt+F4 -> WM_CLOSE hwnd=0x%lx",
                        (unsigned long)msg->hwnd);
            break;
        }
        msg->message = rb_is_system_key_event(&sdl->key) ? WM_SYSKEYDOWN : WM_KEYDOWN;
        msg->wParam  = (uint32_t)vk;
        msg->lParam  = rb_build_key_lparam(&sdl->key, 0);
        msg->time = (uint32_t)sdl->key.timestamp;
        break;
    }

    case SDL_KEYUP:
    {
        int vk;
        if (!msg->hwnd)
            msg->hwnd = g_active_window;
        if (!msg->hwnd)
            return 0;
        vk = rb_keycode_to_vk(sdl->key.keysym.sym, sdl->key.keysym.scancode);
        if (vk < 0)
            return 0;
        if (rb_is_system_key_event(&sdl->key) && vk == VK_F4) {
            if (sdl->key.keysym.sym == SDLK_LALT || sdl->key.keysym.sym == SDLK_RALT)
                g_alt_key_down = 0;
            return 0;
        }
        msg->message = rb_is_system_key_event(&sdl->key) ? WM_SYSKEYUP : WM_KEYUP;
        msg->wParam  = (uint32_t)vk;
        msg->lParam  = rb_build_key_lparam(&sdl->key, 1);
        msg->time    = (uint32_t)sdl->key.timestamp;
        if (sdl->key.keysym.sym == SDLK_LALT || sdl->key.keysym.sym == SDLK_RALT)
            g_alt_key_down = 0;
        break;
    }

    case SDL_MOUSEMOTION:
        if (!msg->hwnd)
            return 0;
        msg->message = WM_MOUSEMOVE;
        msg->pt_x    = sdl->motion.x;
        msg->pt_y    = sdl->motion.y;
        msg->wParam  = sdl->motion.state; /* mouse button state mask */
        msg->lParam  = (sdl->motion.x | (sdl->motion.y << 16));
        msg->time    = (uint32_t)sdl->motion.timestamp;
        break;

    case SDL_MOUSEBUTTONDOWN:
        if (!msg->hwnd)
            return 0;
        if (sdl->button.button == SDL_BUTTON_LEFT) {
            msg->message = WM_LBUTTONDOWN;
        } else if (sdl->button.button == SDL_BUTTON_RIGHT) {
            msg->message = WM_RBUTTONDOWN;
        } else if (sdl->button.button == SDL_BUTTON_MIDDLE) {
            msg->message = 0x0207; /* WM_MBUTTONDOWN */
        } else {
            return 0;
        }
        msg->pt_x   = sdl->button.x;
        msg->pt_y   = sdl->button.y;
        msg->wParam = 0;
        msg->lParam = (sdl->button.x | (sdl->button.y << 16));
        msg->time   = (uint32_t)sdl->button.timestamp;
        break;

    case SDL_MOUSEBUTTONUP:
        if (!msg->hwnd)
            return 0;
        if (sdl->button.button == SDL_BUTTON_LEFT) {
            msg->message = WM_LBUTTONUP;
        } else if (sdl->button.button == SDL_BUTTON_RIGHT) {
            msg->message = WM_RBUTTONUP;
        } else if (sdl->button.button == SDL_BUTTON_MIDDLE) {
            msg->message = 0x0208; /* WM_MBUTTONUP */
        } else {
            return 0;
        }
        msg->pt_x   = sdl->button.x;
        msg->pt_y   = sdl->button.y;
        msg->wParam = 0;
        msg->lParam = (sdl->button.x | (sdl->button.y << 16)) | (1 << 31);
        msg->time   = (uint32_t)sdl->button.timestamp;
        break;

    case SDL_MOUSEWHEEL:
        if (!msg->hwnd)
            return 0;
        /* Map wheel to WM_MOUSEWHEEL (0x020A) */
        msg->message = 0x020A;
        msg->wParam  = sdl->wheel.y * 120; /* 120 = WHEEL_DELTA */
        msg->pt_x    = sdl->wheel.x;
        msg->pt_y    = sdl->wheel.y;
        msg->lParam  = (sdl->wheel.x | (sdl->wheel.y << 16));
        msg->time    = (uint32_t)sdl->wheel.timestamp;
        break;

    case SDL_WINDOWEVENT:
        if (sdl->window.event == SDL_WINDOWEVENT_FOCUS_GAINED && event_hwnd) {
            g_active_window = event_hwnd;
            return 0;
        }
        if (sdl->window.event == SDL_WINDOWEVENT_FOCUS_LOST &&
            event_hwnd && g_active_window == event_hwnd) {
            g_active_window = 0;
            g_alt_key_down = 0;
            return 0;
        }
        if (!msg->hwnd)
            return 0;
        switch (sdl->window.event) {
        case SDL_WINDOWEVENT_EXPOSED:
        case SDL_WINDOWEVENT_SIZE_CHANGED:
        case SDL_WINDOWEVENT_RESIZED:
            repaint_active_window_black(sdl->window.windowID);
            return 0;

        case SDL_WINDOWEVENT_MOVED:
            return 0;

        case SDL_WINDOWEVENT_CLOSE:
            msg->message = WM_CLOSE;
            msg->wParam  = 0;
            msg->lParam  = 0;
            msg->time    = (uint32_t)sdl->window.timestamp;
            DEBUG_WRITE_ERR("rb_event: SDL_WINDOWEVENT_CLOSE -> WM_CLOSE\n",
                            sizeof("rb_event: SDL_WINDOWEVENT_CLOSE -> WM_CLOSE\n") - 1);
            DEBUG_LEVEL(1,
                        "rb_event: SDL_WINDOWEVENT_CLOSE -> WM_CLOSE hwnd=0x%lx window_id=%u",
                        (unsigned long)msg->hwnd,
                        (unsigned)sdl->window.windowID);
            break;

        case SDL_WINDOWEVENT_MINIMIZED:
            return 0;

        case SDL_WINDOWEVENT_RESTORED:
            return 0;

        default:
            return 0; /* unhandled window sub-event */
        }
        break;

    case SDL_TEXTINPUT:
        if (!msg->hwnd)
            msg->hwnd = g_active_window;
        if (!msg->hwnd)
            return 0;
        msg->message = WM_CHAR;
        msg->wParam  = sdl->text.text[0];
        msg->lParam  = 0;
        msg->time    = (uint32_t)sdl->text.timestamp;
        break;

    case SDL_QUIT:
        msg->message = WM_QUIT;
        msg->wParam  = 0;
        msg->lParam  = 0;
        msg->time    = 0;
        break;

    default:
        return 0; /* unknown event type */
    }

    return 1;
}

static int rb_event_translate_bad_window(rb_msg_t *out_msg)
{
    uintptr_t native_window_id = rb_x11_consume_bad_window();
    uintptr_t hwnd;

    if (!native_window_id)
        return 0;

    hwnd = rb_event_resolve_hwnd_from_native_window(native_window_id);
    if (!hwnd)
        return 0;

    memset(out_msg, 0, sizeof(*out_msg));
    out_msg->hwnd = hwnd;
    out_msg->message = WM_CLOSE;
    DEBUG_LEVEL(1, "rb_event: bad X11 window 0x%lx -> WM_CLOSE hwnd=0x%lx",
                (unsigned long)native_window_id,
                (unsigned long)hwnd);
    return 1;
}

/* ---- Public API ---- */

int rb_event_wait(rb_msg_t *out_msg)
{
    SDL_Event sdl_ev;

    for (;;) {
        if (rb_runtime_consume_shutdown_request())
            rb_event_begin_shutdown();
        if (rb_event_translate_shutdown(out_msg))
            return 1;
        if (rb_event_translate_bad_window(out_msg))
            return 1;

        int wait_ret = (int)rb_call_on_host_stack(rb_sdl_wait_event_call, &sdl_ev);
        if (wait_ret) {
            if (translate_sdl_event(&sdl_ev, out_msg)) {
                return (out_msg->message == WM_QUIT) ? 0 : 1;
            }
            /* Unknown event — discard and keep waiting */
        }
    }
}

int rb_event_peek(rb_msg_t *out_msg)
{
    SDL_Event sdl_ev;

    if (rb_runtime_consume_shutdown_request())
        rb_event_begin_shutdown();
    if (rb_event_translate_shutdown(out_msg))
        return 1;
    if (rb_event_translate_bad_window(out_msg)) {
        return 1;
    }

    int ret = (int)rb_call_on_host_stack(rb_sdl_peep_event_call, &sdl_ev);

    if (ret > 0) {
        if (translate_sdl_event(&sdl_ev, out_msg)) {
            return 1;
        }
        /* Event was consumed but untranslatable — report empty */
        return 0;
    }

    return 0; /* queue empty */
}

int rb_event_push(rb_msg_t *msg)
{
    SDL_Event sdl_ev;
    memset(&sdl_ev, 0, sizeof(sdl_ev));

    switch (msg->message) {
    case WM_QUIT:
        sdl_ev.type = SDL_QUIT;
        break;

    case WM_KEYDOWN:
        sdl_ev.key.type         = SDL_KEYDOWN;
        sdl_ev.key.windowID     = rb_event_get_sdl_window_id(msg->hwnd);
        sdl_ev.key.state        = SDL_PRESSED;
        sdl_ev.key.keysym.sym   = (SDL_Keycode)msg->wParam;
        sdl_ev.key.keysym.scancode = (SDL_Scancode)(msg->lParam >> 16);
        break;

    case WM_KEYUP:
        sdl_ev.key.type         = SDL_KEYUP;
        sdl_ev.key.windowID     = rb_event_get_sdl_window_id(msg->hwnd);
        sdl_ev.key.state        = SDL_RELEASED;
        sdl_ev.key.keysym.sym   = (SDL_Keycode)msg->wParam;
        sdl_ev.key.keysym.scancode = (SDL_Scancode)(msg->lParam >> 16);
        break;

    case WM_LBUTTONDOWN:
        sdl_ev.button.type     = SDL_MOUSEBUTTONDOWN;
        sdl_ev.button.windowID = rb_event_get_sdl_window_id(msg->hwnd);
        sdl_ev.button.button   = SDL_BUTTON_LEFT;
        sdl_ev.button.state    = SDL_PRESSED;
        sdl_ev.button.x        = msg->pt_x;
        sdl_ev.button.y        = msg->pt_y;
        break;

    case WM_LBUTTONUP:
        sdl_ev.button.type     = SDL_MOUSEBUTTONUP;
        sdl_ev.button.windowID = rb_event_get_sdl_window_id(msg->hwnd);
        sdl_ev.button.button   = SDL_BUTTON_LEFT;
        sdl_ev.button.state    = SDL_RELEASED;
        sdl_ev.button.x        = msg->pt_x;
        sdl_ev.button.y        = msg->pt_y;
        break;

    case WM_RBUTTONDOWN:
        sdl_ev.button.type     = SDL_MOUSEBUTTONDOWN;
        sdl_ev.button.windowID = rb_event_get_sdl_window_id(msg->hwnd);
        sdl_ev.button.button   = SDL_BUTTON_RIGHT;
        sdl_ev.button.state    = SDL_PRESSED;
        sdl_ev.button.x        = msg->pt_x;
        sdl_ev.button.y        = msg->pt_y;
        break;

    case WM_RBUTTONUP:
        sdl_ev.button.type     = SDL_MOUSEBUTTONUP;
        sdl_ev.button.windowID = rb_event_get_sdl_window_id(msg->hwnd);
        sdl_ev.button.button   = SDL_BUTTON_RIGHT;
        sdl_ev.button.state    = SDL_RELEASED;
        sdl_ev.button.x        = msg->pt_x;
        sdl_ev.button.y        = msg->pt_y;
        break;

    case WM_MOUSEMOVE:
        sdl_ev.motion.type     = SDL_MOUSEMOTION;
        sdl_ev.motion.windowID = rb_event_get_sdl_window_id(msg->hwnd);
        sdl_ev.motion.state    = msg->wParam;
        sdl_ev.motion.x        = msg->pt_x;
        sdl_ev.motion.y        = msg->pt_y;
        break;

    case WM_SIZE:
        sdl_ev.window.type      = SDL_WINDOWEVENT;
        sdl_ev.window.windowID  = rb_event_get_sdl_window_id(msg->hwnd);
        sdl_ev.window.event     = SDL_WINDOWEVENT_RESIZED;
        sdl_ev.window.data1     = msg->wParam;
        sdl_ev.window.data2     = (uint32_t)msg->lParam;
        break;

    case WM_MOVE:
        sdl_ev.window.type      = SDL_WINDOWEVENT;
        sdl_ev.window.windowID  = rb_event_get_sdl_window_id(msg->hwnd);
        sdl_ev.window.event     = SDL_WINDOWEVENT_MOVED;
        sdl_ev.window.data1     = msg->wParam;
        sdl_ev.window.data2     = (uint32_t)msg->lParam;
        break;

    case WM_CLOSE:
        sdl_ev.window.type      = SDL_WINDOWEVENT;
        sdl_ev.window.windowID  = rb_event_get_sdl_window_id(msg->hwnd);
        sdl_ev.window.event     = SDL_WINDOWEVENT_CLOSE;
        break;

    case WM_CHAR:
        sdl_ev.text.type        = SDL_TEXTINPUT;
        sdl_ev.text.windowID    = rb_event_get_sdl_window_id(msg->hwnd);
        sdl_ev.text.text[0]     = (char)msg->wParam;
        sdl_ev.text.text[1]     = '\0';
        break;

    default:
        /* Unhandled message type — no-op push */
        return RB_OK;
    }

    rb_call_on_host_stack(rb_sdl_push_event_call, &sdl_ev);
    return RB_OK;
}
