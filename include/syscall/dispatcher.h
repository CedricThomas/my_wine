/*
 * dispatcher.h — NT syscall dispatcher
 *
 * Maps Windows NT syscall numbers to C handler functions.
 * Decodes arguments from the ucontext using the x86_64 Windows
 * calling convention (RCX, RDX, R8, R9, ...) and dispatches
 * to the appropriate handler.
 */

#ifndef SYSCALL_DISPATCHER_H
#define SYSCALL_DISPATCHER_H

#include <stdint.h>
#include <sys/ucontext.h>

/**
 * Dispatch a Windows NT syscall to its handler.
 *
 * @syscall_number: the raw NT syscall number (passed directly by thunks,
 *                  e.g. 0x05 for NtCallbackReturn)
 * @ctx:            pointer to the ucontext_t captured by the thunk entry
 *
 * Decodes arguments from the x86_64 Windows calling convention.
 * Writes the return value into gregs[REG_RAX].
 *
 * Returns 0 on success, -1 on unhandled or failed syscall.
 */
int handle_syscall(uint64_t syscall_number, ucontext_t *ctx);

#endif /* SYSCALL_DISPATCHER_H */
