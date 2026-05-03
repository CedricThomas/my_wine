/*
 * dispatcher.h — NT syscall dispatcher
 *
 * Maps Windows NT syscall numbers to C handler functions.
 * Decodes arguments from the ucontext using the x86_64 Windows
 * calling convention (RCX, RDX, R8, R9, ...) and dispatches
 * to the appropriate handler.
 *
 * ── 0xF000 Wine Syscall Offset Convention ─────────────────────
 *
 * Wine uses syscall numbers in the range 0xF000+ to avoid conflicts
 * with real Linux syscalls. The seccomp filter traps every syscall
 * with number >= 0xF000, sending SIGSYS to our handler. The dispatcher
 * strips the 0xF000 offset (syscall_number - 0xF000) to obtain the
 * actual NT syscall number (e.g., 0xF005 → 0x05 for NtCallbackReturn).
 * Linux syscalls with number < 0xF000 pass through the seccomp filter
 * unmodified and execute natively.
 */

#ifndef SYSCALL_DISPATCHER_H
#define SYSCALL_DISPATCHER_H

#include <stdint.h>
#include <sys/ucontext.h>

/**
 * Dispatch a Windows NT syscall to its handler.
 *
 * @syscall_number: the raw syscall number from si_syscall (includes
 *                  the 0xF000 Wine offset, e.g. 0xF005 for NtCallbackReturn)
 * @ctx:            pointer to the ucontext_t captured by the SIGSYS handler
 *
 * Decodes arguments from the x86_64 Windows calling convention.
 * Writes the return value into gregs[REG_RAX].
 *
 * Returns 0 on success, -1 on unhandled or failed syscall.
 */
int handle_syscall(uint64_t syscall_number, ucontext_t *ctx);

#endif /* SYSCALL_DISPATCHER_H */
