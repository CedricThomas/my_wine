/*
 * dispatcher_entry.S — Wine-style stack-switching dispatcher entry
 *
 * Called via `call __wine_dispatcher` from generated thunks.
 * On entry: RDI = syscall number, [RSP] = return address (pushed by thunk),
 *           RCX/RDX/R8/R9 = guest args, RSP = guest stack pointer.
 *
 * Saves all guest state to __wine_guest_regs global struct, switches
 * to UNIX stack, calls c_dispatch_syscall(), restores state, returns.
 *
 * NO RED ZONE — this function must not be compiled with red zone,
 * as the guest stack is still active during stack switch.
 */

.section .text

/* Offsets into guest_regs struct (all uint64_t = 8 bytes each) */
.equ RCX_OFF,       0
.equ RDX_OFF,       8
.equ R8_OFF,        16
.equ R9_OFF,        24
.equ RSP_OFF,       32
.equ RET_ADDR_OFF,  40
.equ RAX_OFF,       48

.extern __wine_guest_regs
.extern unix_stack_ptr_val
.extern c_dispatch_syscall

.globl __wine_dispatcher
.type __wine_dispatcher, @function
.code64

__wine_dispatcher:
    /* ── Save guest state to __wine_guest_regs ── */

    /* Return address: currently at [RSP] (pushed by thunk `call`) */
    mov (%rsp), %rax
    mov %rax, __wine_guest_regs+RET_ADDR_OFF(%rip)

    /* Current guest RSP */
    mov %rsp, __wine_guest_regs+RSP_OFF(%rip)

    /* Guest input args (Windows x64 ABI: RCX, RDX, R8, R9) */
    mov %rcx, __wine_guest_regs+RCX_OFF(%rip)
    mov %rdx, __wine_guest_regs+RDX_OFF(%rip)
    mov %r8,  __wine_guest_regs+R8_OFF(%rip)
    mov %r9,  __wine_guest_regs+R9_OFF(%rip)

    /* ── Switch to UNIX stack ── */

    /* Load unix_stack_ptr_val into RAX, subtract 8 for ABI stack alignment.
     * System V ABI: before `call`, RSP must be 8 mod 16 so that after the
     * 8-byte return address is pushed, RSP is 16-byte aligned inside the callee.
     * unix_stack_ptr_val points to base+128KB (16-byte aligned), so
     * unix_stack_ptr_val - 8 gives us 8 mod 16. */
    mov unix_stack_ptr_val(%rip), %rsp
    sub $8, %rsp

    /* ── Call C dispatcher (RDI already = syscall_nr) ── */

    call c_dispatch_syscall

    /* ── Save result ── */

    mov %rax, __wine_guest_regs+RAX_OFF(%rip)

    /* ── Switch back to guest stack ── */

    mov __wine_guest_regs+RSP_OFF(%rip), %rsp

    /* Restore guest registers ── */

    mov __wine_guest_regs+RCX_OFF(%rip), %rcx
    mov __wine_guest_regs+RDX_OFF(%rip), %rdx
    mov __wine_guest_regs+R8_OFF(%rip), %r8
    mov __wine_guest_regs+R9_OFF(%rip), %r9
    mov __wine_guest_regs+RAX_OFF(%rip), %rax

    /* Pop the thunk's return address from guest stack (pushed by thunk's `call`).
     * After this, the guest caller's original return address is at [RSP],
     * so the subsequent `ret` returns directly to the guest caller.
     */
    add $8, %rsp

    /* Return to guest caller */

    ret
