/*
 * dispatcher_entry.h
 *
 * Interface between the assembly dispatcher entry (dispatcher_entry_asm.S) and
 * the C dispatcher (dispatcher.c) for Wine-style single-process syscall
 * dispatching.
 *
 * The assembly entry saves the guest registers into __wine_guest_regs,
 * switches to the pre-allocated UNIX stack, calls c_dispatch_syscall(),
 * then restores the guest state. This header defines the shared structures
 * and symbols used by both sides.
 */

#ifndef DISPATCHER_ENTRY_H
#define DISPATCHER_ENTRY_H

#include <stdint.h>

/*
 * Guest register snapshot at dispatch time.
 * Input arguments follow the Windows x64 calling convention
 * (RCX, RDX, R8, R9). The remaining fields capture the guest
 * stack pointer, thunk return address, and the output result.
 */
struct guest_regs {
    uint64_t rcx;       /* guest input arg 1 */
    uint64_t rdx;       /* guest input arg 2 */
    uint64_t r8;        /* guest input arg 3 */
    uint64_t r9;        /* guest input arg 4 */
    uint64_t rsp;       /* original guest RSP at dispatch time */
    uint64_t ret_addr;  /* return address pushed by the thunk call */
    uint64_t rax;       /* output: result for guest */
};

/* Shared global state between assembly dispatcher and C dispatcher */
extern struct guest_regs __wine_guest_regs;

/* Top of the pre-allocated UNIX stack (16-byte aligned) */
extern void *unix_stack_ptr_val;

/* C dispatcher entry point — implemented in dispatcher.c */
uint64_t c_dispatch_syscall(uint64_t nr);

#endif /* DISPATCHER_ENTRY_H */
