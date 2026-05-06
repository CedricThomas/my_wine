/*
 * dispatcher_entry.S — Wine-style stack-switching dispatcher entry
 *
 * Called via `call __wine_dispatcher` from generated thunks.
 * On entry: RDI = syscall number, [RSP] = return address (pushed by thunk's call),
 *           [RSP+8] = original RDI (pushed by thunk's `push rdi`),
 *           [RSP+16] = guest caller's return address (pushed by guest's call to imported func),
 *           RCX/RDX/R8/R9 = guest args, RSI = callee-saved, RSP = guest stack pointer.
 *
 * Saves all guest state EXCEPT RDI (the thunk handles RDI save/restore with push/pop
 * so the dispatcher's `ret` lands on the thunk's `pop rdi`).
 * Switches to UNIX stack, calls c_dispatch_syscall(), restores state, returns.
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
.equ RDI_OFF,       32
.equ RSI_OFF,       40
.equ RSP_OFF,       48
.equ RET_ADDR_OFF,  56
.equ RAX_OFF,       64
.equ RBX_OFF,       72
.equ R12_OFF,       80
.equ R13_OFF,       88
.equ R14_OFF,       96
.equ R15_OFF,       104

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

    /* Guest callee-saved registers (clobbered by C dispatcher) */
    /* RDI is NOT saved here — the thunk push/pop rdi wraps the call. */
    mov %rsi, __wine_guest_regs+RSI_OFF(%rip)
    mov %rbx, __wine_guest_regs+RBX_OFF(%rip)
    mov %r12, __wine_guest_regs+R12_OFF(%rip)
    mov %r13, __wine_guest_regs+R13_OFF(%rip)
    mov %r14, __wine_guest_regs+R14_OFF(%rip)
    mov %r15, __wine_guest_regs+R15_OFF(%rip)

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
    /* RDI is NOT restored here — the thunk's `pop rdi` after our `ret` handles it. */
    mov __wine_guest_regs+RSI_OFF(%rip), %rsi
    mov __wine_guest_regs+RBX_OFF(%rip), %rbx
    mov __wine_guest_regs+R12_OFF(%rip), %r12
    mov __wine_guest_regs+R13_OFF(%rip), %r13
    mov __wine_guest_regs+R14_OFF(%rip), %r14
    mov __wine_guest_regs+R15_OFF(%rip), %r15
    mov __wine_guest_regs+RAX_OFF(%rip), %rax

    /* Return to the thunk's `pop rdi` (at loc+14, pushed by the thunk's `call`).
     * The thunk then does: pop rdi (restores original), ret (returns to guest caller).
     */

    ret
