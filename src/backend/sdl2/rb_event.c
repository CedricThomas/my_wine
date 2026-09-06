/*
 * rb_event.c
 *
 * SDL2 backend — event system.
 * Translates SDL events to Windows MSG-compatible rb_msg_t and vice versa.
 */

#include "rb_sdl2_priv.h"
#include <string.h>

/* ---- Windows message constants ---- */

#define WM_CREATE         0x0001
#define WM_QUIT           0x0012
#define WM_SYSCOMMAND     0x0112

#define SC_MINIMIZE       0xF020
#define SC_CLOSE          0xF060
#define SC_RESTORE        0xF120

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

int rb_event_translate_sdl_event(SDL_Event *sdl, rb_msg_t *msg)
{
    uintptr_t event_hwnd = rb_event_get_window_hwnd(sdl);

    memset(msg, 0, sizeof(*msg));
    msg->hwnd = event_hwnd;

    switch (sdl->type) {
    case SDL_KEYDOWN:
    case SDL_KEYUP:
    case SDL_TEXTINPUT:
        return rb_event_translate_keyboard_or_text(sdl, msg);

    case SDL_MOUSEMOTION:
    case SDL_MOUSEBUTTONDOWN:
    case SDL_MOUSEBUTTONUP:
    case SDL_MOUSEWHEEL:
    case SDL_WINDOWEVENT:
        return rb_event_translate_window_or_mouse(sdl, event_hwnd, msg);

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
