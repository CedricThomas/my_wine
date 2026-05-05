#ifndef MY_WINE_DEBUG_H
#define MY_WINE_DEBUG_H

#include <stdio.h>
#include <unistd.h>

/*
 * Debug Output Macros
 *
 * Controlled by the global g_debug_enabled (set from MY_WINE_DEBUG
 * env var in main.c before any guest code runs).
 * When non-zero, debug output is enabled.
 * When zero, all debug output is a no-op.
 *
 * Usage:
 *   DEBUG("value = %d", val);
 *   DEBUG_WRITE_ERR("error", 5);
 *
 * NOTE: These macros are for informational/trace output only.
 * ERROR and WARNING messages should NOT use these macros — they must
 * always be visible regardless of the debug setting.
 */

/* Set from main.c by scanning envp for MY_WINE_DEBUG.
 * Weak symbol: defaults to 0 when common.o isn't linked (tests). */
extern __attribute__((weak)) int g_debug_enabled;

/*
 * DEBUG(fmt, ...)
 *
 * Prints a formatted message to stderr only when
 * g_debug_enabled is set.
 * Automatically appends a newline. Safe for use anywhere in regular
 * code (not signal handlers).
 */
#define DEBUG(fmt, ...)                                               \
    do {                                                              \
        if (g_debug_enabled) {                                        \
            fprintf(stderr, fmt "\n", ##__VA_ARGS__);                 \
        }                                                             \
    } while (0)

/*
 * DEBUG_WRITE_ERR(msg, len)
 *
 * Writes `len` bytes from `msg` to stderr using write().
 * Only active when g_debug_enabled is set.
 * Signal-safe: uses write() instead of printf, so it can be used
 * inside signal handlers without risk of deadlock.
 */
#define DEBUG_WRITE_ERR(msg, len)                                     \
    do {                                                              \
        if (g_debug_enabled) {                                        \
            write(STDERR_FILENO, (msg), (len));                       \
        }                                                             \
    } while (0)

#endif /* MY_WINE_DEBUG_H */
