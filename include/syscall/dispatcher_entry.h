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

#if defined(__i386__)
/*
 * 32-bit guest register snapshot at dispatch time.
 * Input arguments follow the Windows x86 cdecl calling convention
 * (all on stack). The assembly dispatcher saves EAX-EBP, EFLAGS,
 * the thunk return EIP, and the original ESP before switching
 * to the UNIX stack.
 */
struct guest_regs {
    uint32_t eax;       /* output: result for guest */
    uint32_t ebx;       /* callee-saved */
    uint32_t ecx;       /* volatile */
    uint32_t edx;       /* volatile (syscall nr from thunk) */
    uint32_t esi;       /* callee-saved */
    uint32_t edi;       /* callee-saved */
    uint32_t ebp;       /* frame pointer / callee-saved */
    uint32_t esp;       /* original guest ESP at dispatch time */
    uint32_t eip;       /* return address pushed by thunk call */
    uint32_t eflags;    /* guest EFLAGS at dispatch time */
};
#else
/*
 * 64-bit guest register snapshot at dispatch time.
 * Input arguments follow the Windows x64 calling convention
 * (RCX, RDX, R8, R9). The remaining fields capture the guest
 * stack pointer, thunk return address, and the output result.
 */
struct guest_regs {
    uint64_t rcx;       /* guest input arg 1 */
    uint64_t rdx;       /* guest input arg 2 */
    uint64_t r8;        /* guest input arg 3 */
    uint64_t r9;        /* guest input arg 4 */
    uint64_t rdi;       /* guest callee-saved (clobbered by thunk) */
    uint64_t rsi;       /* guest callee-saved (clobbered by dispatcher) */
    uint64_t rsp;       /* original guest RSP at dispatch time */
    uint64_t ret_addr;  /* return address pushed by the thunk call */
    uint64_t rax;       /* output: result for guest */
    uint64_t rbx;       /* guest callee-saved */
    uint64_t r12;       /* guest callee-saved */
    uint64_t r13;       /* guest callee-saved */
    uint64_t r14;       /* guest callee-saved */
    uint64_t r15;       /* guest callee-saved */
};
#endif

/* Shared global state between assembly dispatcher and C dispatcher */
extern struct guest_regs __wine_guest_regs;

/* Top of the pre-allocated UNIX stack (16-byte aligned) */
extern void *unix_stack_ptr_val;

/* C dispatcher entry point — implemented in dispatcher.c */
#if defined(__i386__)
uint32_t c_dispatch_syscall(uint32_t nr);
#else
uint64_t c_dispatch_syscall(uint64_t nr);
#endif

/* Guest pointer type in native width */
typedef uintptr_t wine_gptr;

/* Return the address of the assembly dispatcher for thunk generation */
void *wine_dispatcher_addr(void);

/* Allocate and switch to the UNIX stack for dispatcher use */
int setup_unix_stack(void);

#endif /* DISPATCHER_ENTRY_H */
