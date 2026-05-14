#ifndef MY_WINE_DEBUG_H
#define MY_WINE_DEBUG_H

#include <stdio.h>
#include <unistd.h>

/*
 * Debug Output Macros
 *
 * Controlled by the global debug level:
 *   0 quiet (default)
 *   1 setup milestones and warnings
 *   2 loader/syscall trace summaries
 *   3 very verbose per-symbol/per-IAT diagnostics
 *
 * Usage:
 *   DEBUG("value = %d", val);          // level 1
 *   DEBUG_LEVEL(2, "value = %d", val); // level 2+
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
 * g_debug_level. In test binaries (with debug.c), the pointer points
 * to the default handler → returns 0, no crash.
 *
 * The weak pointer's address is in .data.rel.ro (valid to read).
 * Only its *value* is 0 when undefined, and we check for that.
 */
extern int (*debug_check_fn)(void) __attribute__((weak));
extern int (*debug_level_fn)(void) __attribute__((weak));

static inline int debug_get_level(void) {
    if (debug_level_fn) return debug_level_fn();
    if (debug_check_fn) return debug_check_fn() ? 1 : 0;
    return 0;
}

static inline int debug_is_enabled(void) {
    return debug_get_level() >= 1;
}

static inline int debug_level_at_least(int level) {
    return debug_get_level() >= level;
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
        if (debug_level_at_least(1)) {                                \
            fprintf(stderr, fmt "\n", ##__VA_ARGS__);                 \
        }                                                             \
    } while (0)

#define DEBUG_LEVEL(level, fmt, ...)                                  \
    do {                                                              \
        if (debug_level_at_least(level)) {                            \
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
        if (debug_level_at_least(1)) {                                \
            write(STDERR_FILENO, (msg), (len));                       \
        }                                                             \
    } while (0)

#define DEBUG_WRITE_ERR_LEVEL(level, msg, len)                        \
    do {                                                              \
        if (debug_level_at_least(level)) {                            \
            write(STDERR_FILENO, (msg), (len));                       \
        }                                                             \
    } while (0)

#endif /* MY_WINE_DEBUG_H */
