#ifndef MY_WINE_DEBUG_H
#define MY_WINE_DEBUG_H

#include <stdio.h>
#include <unistd.h>

/*
 * Debug Output Macros
 *
 * Controlled by the global debug flag. When enabled, debug output is active.
 * When disabled (default), all debug output is a no-op.
 *
 * Usage:
 *   DEBUG("value = %d", val);
 *   DEBUG_WRITE_ERR("error", 5);
 *
 * NOTE: These macros are for informational/trace output only.
 * ERROR and WARNING messages should NOT use these macros — they must
 * always be visible regardless of the debug setting.
 */

/*
 * debug_is_enabled() checks if debug output is active.
 *
 * Uses a weak function pointer (debug_check_fn) that defaults to a safe
 * disabled state. In my_wine, common.c overrides the pointer to check
 * g_debug_enabled. In test binaries (with debug.c), the pointer points
 * to the default handler → returns 0, no crash.
 *
 * The weak pointer's address is in .data.rel.ro (valid to read).
 * Only its *value* is 0 when undefined, and we check for that.
 */
extern int (*debug_check_fn)(void) __attribute__((weak));

static inline int debug_is_enabled(void) {
    if (debug_check_fn) return debug_check_fn();
    return 0;
}

/*
 * DEBUG(fmt, ...)
 *
 * Prints a formatted message to stderr only when debug is enabled.
 * Automatically appends a newline. Safe for use anywhere in regular
 * code (not signal handlers).
 */
#define DEBUG(fmt, ...)                                               \
    do {                                                              \
        if (debug_is_enabled()) {                                     \
            fprintf(stderr, fmt "\n", ##__VA_ARGS__);                 \
        }                                                             \
    } while (0)

/*
 * DEBUG_WRITE_ERR(msg, len)
 *
 * Writes `len` bytes from `msg` to stderr using write().
 * Only active when debug is enabled.
 * Signal-safe: uses write() instead of printf, so it can be used
 * inside signal handlers without risk of deadlock.
 */
#define DEBUG_WRITE_ERR(msg, len)                                     \
    do {                                                              \
        if (debug_is_enabled()) {                                     \
            write(STDERR_FILENO, (msg), (len));                       \
        }                                                             \
    } while (0)

#endif /* MY_WINE_DEBUG_H */
