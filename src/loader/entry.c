/*
 * entry.c — Fork orchestration and jump to PE entry point
 *
 * Forks a child process to run the guest PE. In the child, delegates
 * all setup to child_setup.c (signal handlers, SEH, thunks, patches,
 * watchdog, and the actual jump). In the parent, waits for the child
 * and cleans up guest resources.
 */

#define _GNU_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <unistd.h>
#include <sys/wait.h>

#include "loader_priv.h"

/* Guest entry trampoline (implemented in run_guest.S) */
extern void run_guest(void (*)(void), void *, void *, char **, char **,
                       void (*)(uint32_t)) __attribute__((noreturn));

/* From child_setup.c — called in child and parent after fork */
extern void setup_child_and_run(uint64_t entry_abs, void *stack_top,
                                void *teb, char **guest_argv,
                                char **guest_envp);
extern void cleanup_guest(void *teb, void *stack_base);

/**
 * Fork and jump to the PE entry point.
 *
 * In the child process:
 *   - Install signal handlers (SIGSEGV, SIGILL, etc.)
 *   - Set up alternate signal stack
 *   - Generate syscall thunks and install SIGSYS dispatcher
 *   - Re-set GS base to TEB
 *   - Patch __acrt_iob_func thunk to return __wine_iob_data directly
 *   - Jump to the PE's entry point via run_guest
 *
 * In the parent process:
 *   - Wait for child and return its exit code.
 *
 * @param  entry_abs   absolute virtual address of the PE entry point
 * @param  stack_top   top of the guest stack
 * @param  stack_base  base of the guest stack (unused, kept for ABI)
 * @param  teb         TEB pointer (GS base)
 * @param  guest_argv  argument vector for the guest
 * @param  guest_envp  environment pointer for the guest
 * @return  exit code of the child, or -1 on fork failure
 */
int jump_to_entry(uint64_t entry_abs, void *stack_top, void *stack_base, void *teb,
                  char **guest_argv, char **guest_envp)
{
    (void)stack_base;  /* suppress unused warning */
    pid_t pid = fork();

    if (pid < 0) { perror("fork"); return 1; }

    if (pid == 0) {
        setup_child_and_run(entry_abs, stack_top, teb, guest_argv, guest_envp);
    }

    int status;
    waitpid(pid, &status, 0);

    /* Clean up guest resources now that the child has exited */
    cleanup_guest(teb, stack_base);

    if (WIFEXITED(status)) {
        int code = WEXITSTATUS(status);
        fprintf(stderr, "my_wine: child exited with code %d\n", code);
        return code;
    } else if (WIFSIGNALED(status)) {
        int sig = WTERMSIG(status);
        fprintf(stderr, "my_wine: child killed by signal %d\n", sig);
        return 128 + sig;
    }

    return 0;
}
