/*
 * infinite_loop.c — Test long-running process (no watchdog interference)
 *
 * Runs an infinite loop until killed by Ctrl+C (SIGINT).
 * Verifies the shared-process model handles long-running guests.
 * Uses only inline asm to avoid CRT dependencies on unimplemented functions.
 */

#include <stdint.h>

/* Minimal infinite loop — no CRT dependencies */
int main(void)
{
    /* Infinite loop: just spin. No printf/fflush — those require CRT stubs.
     * The loop proves the shared-process model doesn't hang or get killed
     * by a watchdog during long execution. */
    volatile uint64_t counter = 0;
    while (1) {
        counter++;
    }
    return 0; /* never reached */
}
