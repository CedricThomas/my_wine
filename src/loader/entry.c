/*
 * entry.c — Jump to PE entry point (single-process model)
 *
 * Directly calls setup_guest_and_run() which does all setup and
 * calls run_guest(). Since there's no fork, everything runs in
 * the same process. Guest code exits via ExitProcess which calls
 * NtTerminateProcess → exit syscall.
 */

#define _GNU_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <unistd.h>

#include "loader_priv.h"

/* Guest entry trampoline (implemented in run_guest.S) */
extern void run_guest(void (*)(void), void *, void *, char **, char **,
                       void (*)(uint32_t)) __attribute__((noreturn));

/* From guest_setup.c — called directly (no fork) */
__attribute__((noreturn)) void setup_guest_and_run(uint64_t entry_abs, void *image_base, void *stack_top,
                                                    void *teb, char **guest_argv,
                                                    char **guest_envp);

/**
 * Jump to the PE entry point (single-process model).
 *
 * Calls setup_guest_and_run() which:
 *   - Installs signal handlers (SIGSEGV, SIGILL, etc.)
 *   - Sets up alternate signal stack
 *   - Generates syscall thunks and installs dispatcher
 *   - Sets up UNIX stack for syscall dispatch
 *   - Re-sets GS base to TEB
 *   - Patches __acrt_iob_func thunk to return __wine_iob_data directly
 *   - Jumps to the PE's entry point via run_guest
 *
 * After run_guest() "returns" (it shouldn't — ExitProcess exits),
 * the process has already terminated via NtTerminateProcess.
 *
 * @param  entry_abs   absolute virtual address of the PE entry point
 * @param  stack_top   top of the guest stack
 * @param  teb         TEB pointer (GS base)
 * @param  guest_argv  argument vector for the guest
 * @param  guest_envp  environment pointer for the guest
 */
__attribute__((noreturn)) void run_guest_entry(uint64_t entry_abs, void *image_base, void *stack_top, void *teb,
                                                char **guest_argv, char **guest_envp)
{
    setup_guest_and_run(entry_abs, image_base, stack_top, teb, guest_argv, guest_envp);
    /* setup_guest_and_run calls run_guest which is noreturn.
     * If we somehow get here, exit. */
    _exit(1);
}
