/*
 * rb_event.c
 *
 * SDL2 backend — event system.
 * Translates SDL events to Windows MSG-compatible rb_msg_t and vice versa.
 */

#include "rb_sdl2_priv.h"
#include <stdlib.h>
#include <string.h>

/* ---- Active window tracking ---- */

static rb_window_t g_active_window = 0;

void rb_event_set_active_window(rb_window_t win)
{
    g_active_window = win;
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

#define SC_MINIMIZE       0xF020
#define SC_RESTORE        0xF120

/* ---- SDL 2.0.18+ compatibility: 5-arg SDL_PeepEvents ---- */

static inline int rb_peep_events(SDL_Event *ev, int n, SDL_eventaction action)
{
#if SDL_VERSION_ATLEAST(2, 0, 18)
    return SDL_PeepEvents(ev, n, action, 0, 0xFFFFFFFF);
#else
    return SDL_PeepEvents(ev, n, action, 0);
#endif
}

/* ---- translate_sdl_event ----
 * Returns 1 on successful translation, 0 for unknown/untranslatable events. */

static int translate_sdl_event(SDL_Event *sdl, rb_msg_t *msg)
{
    /* Default: no specific window association */
    msg->hwnd = g_active_window;

    switch (sdl->type) {
    case SDL_KEYDOWN:
        msg->message = WM_KEYDOWN;
        msg->wParam  = (uint32_t)sdl->key.keysym.sym;
        msg->lParam  = (sdl->key.keysym.scancode << 16);
        if (sdl->key.repeat) {
            msg->lParam |= (1 << 30); /* repeated */
        }
        msg->time = (uint32_t)sdl->key.timestamp;
        break;

    case SDL_KEYUP:
        msg->message = WM_KEYUP;
        msg->wParam  = (uint32_t)sdl->key.keysym.sym;
        msg->lParam  = (sdl->key.keysym.scancode << 16) | (1 << 31); /* released */
        msg->time    = (uint32_t)sdl->key.timestamp;
        break;

    case SDL_MOUSEMOTION:
        msg->message = WM_MOUSEMOVE;
        msg->pt_x    = sdl->motion.x;
        msg->pt_y    = sdl->motion.y;
        msg->wParam  = sdl->motion.state; /* mouse button state mask */
        msg->lParam  = (sdl->motion.x | (sdl->motion.y << 16));
        msg->time    = (uint32_t)sdl->motion.timestamp;
        break;

    case SDL_MOUSEBUTTONDOWN:
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
        /* Map wheel to WM_MOUSEWHEEL (0x020A) */
        msg->message = 0x020A;
        msg->wParam  = sdl->wheel.y * 120; /* 120 = WHEEL_DELTA */
        msg->pt_x    = sdl->wheel.x;
        msg->pt_y    = sdl->wheel.y;
        msg->lParam  = (sdl->wheel.x | (sdl->wheel.y << 16));
        msg->time    = (uint32_t)sdl->wheel.timestamp;
        break;

    case SDL_WINDOWEVENT:
        switch (sdl->window.event) {
        case SDL_WINDOWEVENT_RESIZED:
            msg->message = WM_SIZE;
            msg->wParam  = sdl->window.data1;  /* width */
            msg->lParam  = sdl->window.data2;  /* height */
            msg->time    = (uint32_t)sdl->window.timestamp;
            break;

        case SDL_WINDOWEVENT_MOVED:
            msg->message = WM_MOVE;
            msg->wParam  = sdl->window.data1;  /* x */
            msg->lParam  = sdl->window.data2;  /* y */
            msg->time    = (uint32_t)sdl->window.timestamp;
            break;

        case SDL_WINDOWEVENT_CLOSE:
            msg->message = WM_CLOSE;
            msg->wParam  = 0;
            msg->lParam  = 0;
            msg->time    = (uint32_t)sdl->window.timestamp;
            break;

        case SDL_WINDOWEVENT_MINIMIZED:
            msg->message = WM_SYSCOMMAND;
            msg->wParam  = SC_MINIMIZE;
            msg->lParam  = 0;
            msg->time    = (uint32_t)sdl->window.timestamp;
            break;

        case SDL_WINDOWEVENT_RESTORED:
            msg->message = WM_SYSCOMMAND;
            msg->wParam  = SC_RESTORE;
            msg->lParam  = 0;
            msg->time    = (uint32_t)sdl->window.timestamp;
            break;

        case SDL_WINDOWEVENT_SHOWN:
            msg->message = WM_CREATE;
            msg->wParam  = 0;
            msg->lParam  = 0;
            msg->time    = (uint32_t)sdl->window.timestamp;
            break;

        default:
            return 0; /* unhandled window sub-event */
        }
        break;

    case SDL_TEXTINPUT:
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

/* ---- Public API ---- */

int rb_event_wait(rb_msg_t *out_msg)
{
    SDL_Event sdl_ev;

    while (SDL_WaitEvent(&sdl_ev)) {
        if (translate_sdl_event(&sdl_ev, out_msg)) {
            return (out_msg->message == WM_QUIT) ? 1 : 0;
        }
        /* Unknown event — discard and keep waiting */
    }

    return -1; /* SDL error */
}

int rb_event_peek(rb_msg_t *out_msg)
{
    SDL_Event sdl_ev;

    if (rb_peep_events(&sdl_ev, 1, SDL_GETEVENT) > 0) {
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
        sdl_ev.key.windowID     = 0;
        sdl_ev.key.state        = SDL_PRESSED;
        sdl_ev.key.keysym.sym   = (SDL_Keycode)msg->wParam;
        sdl_ev.key.keysym.scancode = (SDL_Scancode)(msg->lParam >> 16);
        break;

    case WM_KEYUP:
        sdl_ev.key.type         = SDL_KEYUP;
        sdl_ev.key.windowID     = 0;
        sdl_ev.key.state        = SDL_RELEASED;
        sdl_ev.key.keysym.sym   = (SDL_Keycode)msg->wParam;
        sdl_ev.key.keysym.scancode = (SDL_Scancode)(msg->lParam >> 16);
        break;

    case WM_LBUTTONDOWN:
        sdl_ev.button.type     = SDL_MOUSEBUTTONDOWN;
        sdl_ev.button.windowID = 0;
        sdl_ev.button.button   = SDL_BUTTON_LEFT;
        sdl_ev.button.state    = SDL_PRESSED;
        sdl_ev.button.x        = msg->pt_x;
        sdl_ev.button.y        = msg->pt_y;
        break;

    case WM_LBUTTONUP:
        sdl_ev.button.type     = SDL_MOUSEBUTTONUP;
        sdl_ev.button.windowID = 0;
        sdl_ev.button.button   = SDL_BUTTON_LEFT;
        sdl_ev.button.state    = SDL_RELEASED;
        sdl_ev.button.x        = msg->pt_x;
        sdl_ev.button.y        = msg->pt_y;
        break;

    case WM_RBUTTONDOWN:
        sdl_ev.button.type     = SDL_MOUSEBUTTONDOWN;
        sdl_ev.button.windowID = 0;
        sdl_ev.button.button   = SDL_BUTTON_RIGHT;
        sdl_ev.button.state    = SDL_PRESSED;
        sdl_ev.button.x        = msg->pt_x;
        sdl_ev.button.y        = msg->pt_y;
        break;

    case WM_RBUTTONUP:
        sdl_ev.button.type     = SDL_MOUSEBUTTONUP;
        sdl_ev.button.windowID = 0;
        sdl_ev.button.button   = SDL_BUTTON_RIGHT;
        sdl_ev.button.state    = SDL_RELEASED;
        sdl_ev.button.x        = msg->pt_x;
        sdl_ev.button.y        = msg->pt_y;
        break;

    case WM_MOUSEMOVE:
        sdl_ev.motion.type     = SDL_MOUSEMOTION;
        sdl_ev.motion.windowID = 0;
        sdl_ev.motion.state    = msg->wParam;
        sdl_ev.motion.x        = msg->pt_x;
        sdl_ev.motion.y        = msg->pt_y;
        break;

    case WM_SIZE:
        sdl_ev.window.type      = SDL_WINDOWEVENT;
        sdl_ev.window.windowID  = 0;
        sdl_ev.window.event     = SDL_WINDOWEVENT_RESIZED;
        sdl_ev.window.data1     = msg->wParam;
        sdl_ev.window.data2     = (uint32_t)msg->lParam;
        break;

    case WM_MOVE:
        sdl_ev.window.type      = SDL_WINDOWEVENT;
        sdl_ev.window.windowID  = 0;
        sdl_ev.window.event     = SDL_WINDOWEVENT_MOVED;
        sdl_ev.window.data1     = msg->wParam;
        sdl_ev.window.data2     = (uint32_t)msg->lParam;
        break;

    case WM_CLOSE:
        sdl_ev.window.type      = SDL_WINDOWEVENT;
        sdl_ev.window.windowID  = 0;
        sdl_ev.window.event     = SDL_WINDOWEVENT_CLOSE;
        break;

    case WM_CHAR:
        sdl_ev.text.text[0]     = (char)msg->wParam;
        sdl_ev.text.text[1]     = '\0';
        break;

    default:
        /* Unhandled message type — no-op push */
        return RB_OK;
    }

    SDL_PushEvent(&sdl_ev);
    return RB_OK;
}
