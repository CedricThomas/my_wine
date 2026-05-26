/*
 * rb_event_focus.c
 *
 * Owns backend focus policy: active-window activation, synthetic focus
 * message emission, and ALT/system-key state tracking.
 */

#include "rb_sdl2_priv.h"
#include <string.h>

extern void winmm_doom95_set_application_active(int active) __attribute__((weak));

#define WM_ACTIVATE       0x0006
#define WM_SETFOCUS       0x0007
#define WM_KILLFOCUS      0x0008
#define WA_INACTIVE       0
#define WA_ACTIVE         1

static int g_alt_key_down = 0;

static void rb_event_queue_focus_messages(uintptr_t hwnd, int gained)
{
    rb_msg_t msg;

    if (!hwnd)
        return;

    memset(&msg, 0, sizeof(msg));
    msg.hwnd = hwnd;
    msg.message = WM_ACTIVATE;
    msg.wParam = gained ? WA_ACTIVE : WA_INACTIVE;
    rb_event_push_synthetic(&msg);

    memset(&msg, 0, sizeof(msg));
    msg.hwnd = hwnd;
    msg.message = gained ? WM_SETFOCUS : WM_KILLFOCUS;
    rb_event_push_synthetic(&msg);
}

void rb_event_activate_window(uintptr_t hwnd)
{
    if (!hwnd)
        return;

    rb_event_set_active_window(hwnd);
    rb_event_queue_focus_messages(hwnd, 1);
}

int rb_event_is_system_key_event(const SDL_KeyboardEvent *key)
{
    SDL_Keycode sym = key->keysym.sym;

    if (sym == SDLK_LALT || sym == SDLK_RALT)
        return 1;
    if (g_alt_key_down)
        return 1;

    return (key->keysym.mod & KMOD_ALT) != 0;
}

void rb_event_note_alt_keydown(SDL_Keycode sym)
{
    if (sym == SDLK_LALT || sym == SDLK_RALT)
        g_alt_key_down = 1;
}

void rb_event_note_alt_keyup(SDL_Keycode sym)
{
    if (sym == SDLK_LALT || sym == SDLK_RALT)
        g_alt_key_down = 0;
}

void rb_event_clear_alt_state(void)
{
    g_alt_key_down = 0;
}

int rb_event_translate_focus_window_event(const SDL_WindowEvent *window,
                                          uintptr_t event_hwnd,
                                          rb_msg_t *msg)
{
    if (!window || !event_hwnd || !msg)
        return 0;

    if (window->event == SDL_WINDOWEVENT_FOCUS_GAINED) {
        rb_event_set_active_window(event_hwnd);
        if (winmm_doom95_set_application_active)
            winmm_doom95_set_application_active(1);
        rb_event_queue_focus_messages(event_hwnd, 1);
        return rb_event_pop_synthetic(msg);
    }

    if (window->event == SDL_WINDOWEVENT_FOCUS_LOST &&
        rb_event_get_active_window() == event_hwnd) {
        if (winmm_doom95_set_application_active)
            winmm_doom95_set_application_active(0);
        rb_event_queue_focus_messages(event_hwnd, 0);
        rb_event_set_active_window(0);
        rb_event_clear_alt_state();
        return rb_event_pop_synthetic(msg);
    }

    return 0;
}
