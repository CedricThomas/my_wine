/*
 * dispatcher.h — NT syscall dispatcher
 *
 * Maps Windows NT syscall numbers to C handler functions.
 * Decodes arguments using the x86_64 Windows calling convention
 * (RCX, RDX, R8, R9, ...) and dispatches to the appropriate handler.
 *
 * Two dispatch entry points:
 *   - c_dispatch_syscall(nr): production path for single-process mode.
 *     Reads input from __wine_guest_regs directly (no ucontext).
 *   - handle_syscall(nr, ctx): legacy path kept for backward compatibility
 *     with existing tests that construct ucontext_t manually.
 */

#ifndef SYSCALL_DISPATCHER_H
#define SYSCALL_DISPATCHER_H

#include <stdint.h>
#include <sys/ucontext.h>

/**
 * Dispatch a Windows NT syscall (single-process, no ucontext).
 *
 * Reads input arguments from __wine_guest_regs (populated by the
 * assembly dispatcher entry). Writes result into __wine_guest_regs.rax.
 *
 * @nr  NT syscall number
 *
 * @return result to place in RAX
 */
uint64_t c_dispatch_syscall(uint64_t nr);

/**
 * Dispatch a Windows NT syscall (legacy ucontext-based, for tests).
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
