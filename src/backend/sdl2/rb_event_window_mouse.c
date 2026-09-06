/*
 * rb_event_window_mouse.c
 *
 * Owns mouse and non-focus window-event translation for the SDL backend.
 */

#include "rb_sdl2_priv.h"
#include "include/debug.h"

#define WM_MOVE           0x0003
#define WM_SIZE           0x0005
#define WM_PAINT          0x000F
#define WM_CLOSE          0x0010
#define WM_MOUSEMOVE      0x0200
#define WM_LBUTTONDOWN    0x0201
#define WM_LBUTTONUP      0x0202
#define WM_RBUTTONDOWN    0x0204
#define WM_RBUTTONUP      0x0205

static int rb_event_translate_mouse(SDL_Event *sdl, rb_msg_t *msg)
{
    switch (sdl->type) {
    case SDL_MOUSEMOTION:
        if (!msg->hwnd)
            return 0;
        msg->message = WM_MOUSEMOVE;
        msg->pt_x = sdl->motion.x;
        msg->pt_y = sdl->motion.y;
        msg->wParam = sdl->motion.state;
        msg->lParam = (sdl->motion.x | (sdl->motion.y << 16));
        msg->time = (uint32_t)sdl->motion.timestamp;
        return 1;

    case SDL_MOUSEBUTTONDOWN:
        if (!msg->hwnd)
            return 0;
        if (sdl->button.button == SDL_BUTTON_LEFT) {
            msg->message = WM_LBUTTONDOWN;
        } else if (sdl->button.button == SDL_BUTTON_RIGHT) {
            msg->message = WM_RBUTTONDOWN;
        } else if (sdl->button.button == SDL_BUTTON_MIDDLE) {
            msg->message = 0x0207;
        } else {
            return 0;
        }
        msg->pt_x = sdl->button.x;
        msg->pt_y = sdl->button.y;
        msg->wParam = (uintptr_t)(SDL_GetMouseState(NULL, NULL) & 0xFFFFu);
        msg->lParam = (sdl->button.x | (sdl->button.y << 16));
        msg->time = (uint32_t)sdl->button.timestamp;
        return 1;

    case SDL_MOUSEBUTTONUP:
        if (!msg->hwnd)
            return 0;
        if (sdl->button.button == SDL_BUTTON_LEFT) {
            msg->message = WM_LBUTTONUP;
        } else if (sdl->button.button == SDL_BUTTON_RIGHT) {
            msg->message = WM_RBUTTONUP;
        } else if (sdl->button.button == SDL_BUTTON_MIDDLE) {
            msg->message = 0x0208;
        } else {
            return 0;
        }
        msg->pt_x = sdl->button.x;
        msg->pt_y = sdl->button.y;
        msg->wParam = (uintptr_t)(SDL_GetMouseState(NULL, NULL) & 0xFFFFu);
        msg->lParam = (sdl->button.x | (sdl->button.y << 16)) | (1 << 31);
        msg->time = (uint32_t)sdl->button.timestamp;
        return 1;

    case SDL_MOUSEWHEEL:
        if (!msg->hwnd)
            return 0;
        msg->message = 0x020A;
        msg->wParam = (((uintptr_t)((uint16_t)((int16_t)(sdl->wheel.y * 120)))) << 16) |
                       (uintptr_t)(SDL_GetMouseState(NULL, NULL) & 0xFFFFu);
        msg->pt_x = 0;
        msg->pt_y = 0;
        msg->lParam = 0;
        msg->time = (uint32_t)sdl->wheel.timestamp;
        return 1;

    default:
        return 0;
    }
}

int rb_event_translate_window_or_mouse(SDL_Event *sdl, uintptr_t event_hwnd, rb_msg_t *msg)
{
    if (!sdl || !msg)
        return 0;

    if (rb_event_translate_mouse(sdl, msg))
        return 1;

    if (sdl->type != SDL_WINDOWEVENT)
        return 0;

    if (rb_event_translate_focus_window_event(&sdl->window, event_hwnd, msg))
        return 1;
    if (!msg->hwnd)
        return 0;

    switch (sdl->window.event) {
    case SDL_WINDOWEVENT_EXPOSED:
        msg->message = WM_PAINT;
        msg->wParam = 0;
        msg->lParam = 0;
        msg->time = (uint32_t)sdl->window.timestamp;
        return 1;

    case SDL_WINDOWEVENT_SIZE_CHANGED:
    case SDL_WINDOWEVENT_RESIZED:
        msg->message = WM_SIZE;
        msg->wParam = 0;
        msg->lParam = (int32_t)((sdl->window.data1 & 0xFFFF) |
                                ((sdl->window.data2 & 0xFFFF) << 16));
        msg->time = (uint32_t)sdl->window.timestamp;
        return 1;

    case SDL_WINDOWEVENT_MOVED:
        msg->message = WM_MOVE;
        msg->wParam = 0;
        msg->lParam = (int32_t)((sdl->window.data1 & 0xFFFF) |
                                ((sdl->window.data2 & 0xFFFF) << 16));
        msg->time = (uint32_t)sdl->window.timestamp;
        return 1;

    case SDL_WINDOWEVENT_CLOSE:
        msg->message = WM_CLOSE;
        msg->wParam = 0;
        msg->lParam = 0;
        msg->time = (uint32_t)sdl->window.timestamp;
        DEBUG_WRITE_ERR("rb_event: SDL_WINDOWEVENT_CLOSE -> WM_CLOSE\n",
                        sizeof("rb_event: SDL_WINDOWEVENT_CLOSE -> WM_CLOSE\n") - 1);
        DEBUG_LEVEL(1,
                    "rb_event: SDL_WINDOWEVENT_CLOSE -> WM_CLOSE hwnd=0x%lx window_id=%u",
                    (unsigned long)msg->hwnd,
                    (unsigned)sdl->window.windowID);
        return 1;

    case SDL_WINDOWEVENT_MINIMIZED:
        if (event_hwnd) {
            rb_window *wnd = rb_event_resolve_backend_window(event_hwnd);
            if (wnd) {
                wnd->is_visible = 1;
                wnd->is_minimized = 1;
                wnd->is_maximized = 0;
            }
        }
        return 0;

    case SDL_WINDOWEVENT_RESTORED:
        if (event_hwnd) {
            rb_window *wnd = rb_event_resolve_backend_window(event_hwnd);
            if (wnd) {
                wnd->is_visible = 1;
                wnd->is_minimized = 0;
                wnd->is_maximized = 0;
            }
        }
        return 0;

    case SDL_WINDOWEVENT_MAXIMIZED:
        if (event_hwnd) {
            rb_window *wnd = rb_event_resolve_backend_window(event_hwnd);
            if (wnd) {
                wnd->is_visible = 1;
                wnd->is_minimized = 0;
                wnd->is_maximized = 1;
            }
        }
        return 0;

    default:
        return 0;
    }
}
