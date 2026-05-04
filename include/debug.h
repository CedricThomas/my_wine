#ifndef MY_WINE_DEBUG_H
#define MY_WINE_DEBUG_H

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

/*
 * Debug Output Macros
 *
 * Controlled by the environment variable MY_WINE_DEBUG.
 * When MY_WINE_DEBUG is set to any non-empty value, debug output is enabled.
 * When unset or empty, all debug output is compiled out (no-op).
 *
 * Usage:
 *   DEBUG("value = %d", val);
 *   DEBUG_WRITE_ERR("error", 5);
 *
 * NOTE: These macros are for informational/trace output only.
 * ERROR and WARNING messages should NOT use these macros — they must
 * always be visible regardless of the MY_WINE_DEBUG setting.
 */

/*
 * DEBUG(fmt, ...)
 *
 * Prints a formatted message to stderr only when
 * the MY_WINE_DEBUG environment variable is set.
 * Automatically appends a newline. Safe for use anywhere in regular
 * code (not signal handlers).
 */
#define DEBUG(fmt, ...)                                               \
    do {                                                              \
        if (getenv("MY_WINE_DEBUG")) {                                \
            fprintf(stderr, fmt "\n", ##__VA_ARGS__);                 \
        }                                                             \
    } while (0)

/*
 * DEBUG_WRITE_ERR(msg, len)
 *
 * Writes `len` bytes from `msg` to stderr using write().
 * Only active when MY_WINE_DEBUG is set.
 * Signal-safe: uses write() instead of printf, so it can be used
 * inside signal handlers without risk of deadlock.
 */
#define DEBUG_WRITE_ERR(msg, len)                                     \
    do {                                                              \
        if (getenv("MY_WINE_DEBUG")) {                                \
            write(STDERR_FILENO, (msg), (len));                       \
        }                                                             \
    } while (0)

#endif /* MY_WINE_DEBUG_H */
