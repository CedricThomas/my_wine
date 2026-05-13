/*
 * dispatcher.h — NT syscall dispatcher
 *
 * Maps Windows NT syscall numbers to C handler functions.
 * Decodes arguments and dispatches to the appropriate handler.
 *
 * x86_64: Windows calling convention (RCX, RDX, R8, R9)
 * x86:    cdecl (all args on stack)
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
 * assembly dispatcher entry). Writes result into the appropriate
 * output register (RAX on x86_64, EAX on x86).
 *
 * @nr  NT syscall number
 *
 * @return result to place in the output register
 */
#if defined(__i386__)
uint32_t c_dispatch_syscall(uint32_t nr);
#else
uint64_t c_dispatch_syscall(uint64_t nr);
#endif

#endif /* SYSCALL_DISPATCHER_H */
