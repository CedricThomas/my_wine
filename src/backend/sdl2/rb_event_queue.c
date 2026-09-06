/*
 * rb_event_queue.c
 *
 * Owns SDL host-queue polling and backend message delivery for the SDL2
 * backend. SDL-to-Windows event translation stays in rb_event.c.
 */

#include "rb_sdl2_priv.h"
#include "include/debug.h"
#include <string.h>
#include <time.h>

#define WM_CLOSE          0x0010
#define WM_QUIT           0x0012

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

        if ((int)rb_call_on_host_stack(rb_sdl_wait_event_call, &sdl_ev) > 0) {
            if (rb_event_translate_sdl_event(&sdl_ev, out_msg))
                return (out_msg->message == WM_QUIT) ? 0 : 1;
            /* Unknown event — discard and keep waiting */
        }
    }
}

int rb_event_peek(rb_msg_t *out_msg)
{
    SDL_Event sdl_ev;
    int ret;

    rb_event_maybe_pump_host(1);
    if (rb_event_pop_synthetic(out_msg))
        return 1;
    if (rb_runtime_consume_shutdown_request())
        rb_event_begin_shutdown();
    if (rb_event_translate_shutdown(out_msg))
        return 1;
    if (rb_event_translate_bad_window(out_msg))
        return 1;

    ret = (int)rb_call_on_host_stack(rb_sdl_peep_event_call, &sdl_ev);
    if (ret <= 0)
        return 0;

    if (rb_event_translate_sdl_event(&sdl_ev, out_msg))
        return 1;

    /* Event was consumed but untranslatable — report empty */
    return 0;
}

int rb_event_push(rb_msg_t *msg)
{
    return rb_event_push_synthetic(msg);
}
