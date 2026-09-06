/*
 * rb_runtime_state.c
 *
 * Owns process-global SDL backend runtime state: init/shutdown flags, signal
 * handler installation, and cross-thread shutdown/X11 notifications.
 */

#include "rb_sdl2_priv.h"
#include <signal.h>
#include <string.h>

static int g_backend_initialized = 0;
static uintptr_t g_x11_bad_window_pending = 0;
static volatile sig_atomic_t g_shutdown_requested = 0;
static struct sigaction g_prev_sigint_action;
static struct sigaction g_prev_sigterm_action;
static int g_signal_handlers_installed = 0;

static void rb_shutdown_signal_handler(int signum)
{
    (void)signum;
    g_shutdown_requested = 1;
}

void rb_runtime_set_initialized(int initialized)
{
    g_backend_initialized = initialized ? 1 : 0;
}

int rb_runtime_is_initialized(void)
{
    return g_backend_initialized;
}

void rb_runtime_note_bad_window(uintptr_t native_window_id)
{
    __atomic_store_n(&g_x11_bad_window_pending, native_window_id, __ATOMIC_RELEASE);
}

uintptr_t rb_x11_consume_bad_window(void)
{
    return __atomic_exchange_n(&g_x11_bad_window_pending, 0, __ATOMIC_ACQ_REL);
}

void rb_runtime_note_shutdown_request(void)
{
    g_shutdown_requested = 1;
}

int rb_runtime_consume_shutdown_request(void)
{
    if (!g_shutdown_requested)
        return 0;

    g_shutdown_requested = 0;
    return 1;
}

int rb_runtime_shutdown_requested(void)
{
    return g_shutdown_requested ? 1 : 0;
}

void rb_runtime_install_signal_handlers(void)
{
    struct sigaction sa;

    if (g_signal_handlers_installed)
        return;

    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = rb_shutdown_signal_handler;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGINT, &sa, &g_prev_sigint_action);
    sigaction(SIGTERM, &sa, &g_prev_sigterm_action);
    g_signal_handlers_installed = 1;
}

void rb_runtime_restore_signal_handlers(void)
{
    if (!g_signal_handlers_installed)
        return;

    sigaction(SIGINT, &g_prev_sigint_action, NULL);
    sigaction(SIGTERM, &g_prev_sigterm_action, NULL);
    g_signal_handlers_installed = 0;
}
