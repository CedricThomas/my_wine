/*
 * signal_handler.h — SIGSYS handler and seccomp setup
 *
 * Installs a SIGSYS handler that dispatches to a registered
 * dispatcher function, and sets up a seccomp-BPF filter to
 * trap Wine-offset syscalls (>= 0xF000).
 */

#ifndef SYSCALL_SIGNAL_HANDLER_H
#define SYSCALL_SIGNAL_HANDLER_H

#include <stdint.h>
#include <sys/ucontext.h>

/**
 * Dispatcher function type: receives the (elevated) syscall number
 * and the full ucontext. Returns 0 on success (handler modified
 * the context to produce a result) or non-zero on failure.
 */
typedef int (*dispatcher_func_t)(uint64_t syscall_num, ucontext_t *ctx);

/**
 * Register a generated thunk address so the SIGSYS handler can
 * validate that the call site belongs to one of our thunks.
 * Called from thunk_gen.c after each thunk is mmap'd.
 */
void register_thunk_addr(void *addr);

/**
 * Install the SIGSYS handler with the given dispatcher function.
 * The handler validates the call address and forwards to the
 * dispatcher. Returns 0 on success, -1 on failure.
 */
int setup_sigsys_handler(dispatcher_func_t dispatcher);

/**
 * Install a seccomp-BPF filter that:
 *   - ALLOWs syscalls < 0xF000 (native Linux)
 *   - TRAPs  (SIGSYS) syscalls >= 0xF000 (Wine offset)
 *
 * WARNING: setup_sigsys_handler() must be called BEFORE this.
 * Returns 0 on success, -1 on failure.
 */
int setup_seccomp(void);

#endif /* SYSCALL_SIGNAL_HANDLER_H */
