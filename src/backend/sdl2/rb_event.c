/*
 * rb_event.c
 *
 * SDL2 backend — event system.
 * Translates SDL events to Windows MSG-compatible rb_msg_t and vice versa.
 */

#include "rb_sdl2_priv.h"
#include "include/debug.h"
#include <time.h>
#include <stdlib.h>
#include <string.h>

/* ---- Windows message constants ---- */

#define WM_CREATE         0x0001
#define WM_QUIT           0x0012
#define WM_CLOSE          0x0010
#define WM_SYSCOMMAND     0x0112

#define SC_MINIMIZE       0xF020
#define SC_CLOSE          0xF060
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

static uintptr_t rb_sdl_wait_event_call(void *arg)
{
    return (uintptr_t)SDL_WaitEventTimeout((SDL_Event *)arg, 100);
}

static uintptr_t rb_sdl_peep_event_call(void *arg)
{
    return (uintptr_t)rb_peep_events((SDL_Event *)arg, 1, SDL_GETEVENT);
}

static uintptr_t rb_sdl_pump_events_call(void *arg)
{
    (void)arg;
    SDL_PumpEvents();
    return 0;
}

static uint64_t rb_event_monotonic_ms(void)
{
    struct timespec ts;

    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0)
        return 0;

    return (uint64_t)ts.tv_sec * 1000ULL + (uint64_t)ts.tv_nsec / 1000000ULL;
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

void rb_event_pump_host(void)
{
    rb_call_on_host_stack(rb_sdl_pump_events_call, NULL);
}

void rb_event_maybe_pump_host(uint32_t min_interval_ms)
{
    static uint64_t last_pump_ms = 0;
    static int pump_in_progress = 0;
    uint64_t now_ms;

    if (pump_in_progress)
        return;

    now_ms = rb_event_monotonic_ms();
    if (min_interval_ms != 0 && last_pump_ms != 0 &&
        now_ms != 0 && now_ms - last_pump_ms < (uint64_t)min_interval_ms)
        return;

    pump_in_progress = 1;
    rb_event_pump_host();
    last_pump_ms = (now_ms != 0) ? now_ms : rb_event_monotonic_ms();
    pump_in_progress = 0;
}

int rb_event_wait(rb_msg_t *out_msg)
{
    SDL_Event sdl_ev;

    for (;;) {
        if (rb_event_pop_synthetic(out_msg))
            return (out_msg->message == WM_QUIT) ? 0 : 1;
        if (rb_runtime_consume_shutdown_request())
            rb_event_begin_shutdown();
        if (rb_event_translate_shutdown(out_msg))
            return 1;
        if (rb_event_translate_bad_window(out_msg))
            return 1;

        int wait_ret = (int)rb_call_on_host_stack(rb_sdl_wait_event_call, &sdl_ev);
        if (wait_ret) {
            if (rb_event_translate_sdl_event(&sdl_ev, out_msg)) {
                return (out_msg->message == WM_QUIT) ? 0 : 1;
            }
            /* Unknown event — discard and keep waiting */
        }
    }
}

int rb_event_peek(rb_msg_t *out_msg)
{
    SDL_Event sdl_ev;

    rb_event_maybe_pump_host(1);
    if (rb_event_pop_synthetic(out_msg))
        return 1;
    if (rb_runtime_consume_shutdown_request())
        rb_event_begin_shutdown();
    if (rb_event_translate_shutdown(out_msg))
        return 1;
    if (rb_event_translate_bad_window(out_msg)) {
        return 1;
    }

    int ret = (int)rb_call_on_host_stack(rb_sdl_peep_event_call, &sdl_ev);

    if (ret > 0) {
        if (rb_event_translate_sdl_event(&sdl_ev, out_msg)) {
            return 1;
        }
        /* Event was consumed but untranslatable — report empty */
        return 0;
    }

    return 0; /* queue empty */
}

int rb_event_push(rb_msg_t *msg)
{
    return rb_event_push_synthetic(msg);
}
