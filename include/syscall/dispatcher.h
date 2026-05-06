/*
 * dispatcher.h — NT syscall dispatcher
 *
 * Maps Windows NT syscall numbers to C handler functions.
 * Decodes arguments using the x86_64 Windows calling convention
 * (RCX, RDX, R8, R9, ...) and dispatches to the appropriate handler.
 *
 * Single dispatch entry point:
 *   - c_dispatch_syscall(nr): production path for single-process mode.
 *     Reads input from __wine_guest_regs directly (no ucontext).
 */

#ifndef SYSCALL_DISPATCHER_H
#define SYSCALL_DISPATCHER_H

#include <stdint.h>

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

#endif /* SYSCALL_DISPATCHER_H */
