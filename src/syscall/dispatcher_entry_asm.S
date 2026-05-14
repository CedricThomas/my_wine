/*
 * dispatcher_entry.S — Wine-style stack-switching dispatcher entry
 *
 * Implements the dispatcher entry point for both x86-64 and i386 guests.
 *
 * x86-64 (below): called via `call __wine_dispatcher` from generated thunks.
 *   On entry: RDI = syscall number, RSP = guest stack, RCX/RDX/R8/R9 = guest args.
 *   Saves all guest state EXCEPT RDI (thunk handles it with push/pop),
 *   switches to UNIX stack, calls c_dispatch_syscall(), restores state, returns.
 *
 * i386 (below): called via `call eax` (eax = dispatcher) from generated thunks.
 *   On entry: EDX = syscall number, ESP = guest stack, [ESP] = thunk_ret.
 *   Thunk wraps with push ebp/pop ebp (callee-saved, EBP preserved by thunk).
 *   Dispatcher saves/restores EAX-EBX, ECX-EDX, ESI-EDI, ESP, EIP.
 *   Switches to UNIX stack, calls c_dispatch_syscall(), restores state, returns.
 *
 * NO RED ZONE — this function must not use red zone,
 * as the guest stack is still active during stack switch.
 */

/*
 * x86-64 dispatcher entry.
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
 */

#ifndef __i386__

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

#endif /* !__i386__ */

#if defined(__i386__)

/*
 * 32-bit dispatcher entry.
 *
 * Called via `call eax` from generated thunks (eax = dispatcher address).
 * On entry: EDX = syscall number (set by thunk),
 *           [ESP] = EIP (return address pushed by thunk's `call eax`),
 *           ESP = guest stack pointer at dispatch time.
 *
 * Saves all guest registers (EAX-EBP, ESP, EIP) to __wine_guest_regs,
 * switches to UNIX stack, calls c_dispatch_syscall(edx), restores state,
 * and returns to guest via EIP.
 *
 * NO RED ZONE — this function must not use red zone,
 * as the guest stack is still active during stack switch.
 */

.section .text

/* Offsets into guest_regs struct (all uint32_t = 4 bytes each) */
.equ EAX_OFF,       0
.equ EBX_OFF,       4
.equ ECX_OFF,       8
.equ EDX_OFF,       12
.equ ESI_OFF,       16
.equ EDI_OFF,       20
.equ EBP_OFF,       24
.equ ESP_OFF,       28
.equ EIP_OFF,       32
.equ EFLAGS_OFF,    36

.extern __wine_guest_regs
.extern unix_stack_ptr_val
.extern c_dispatch_syscall

.globl __wine_dispatcher
.type __wine_dispatcher, @function
.code32

__wine_dispatcher:
    /* ── Save guest state to __wine_guest_regs ── */

    /* Stack on entry (thunk does: push ebp; movl $disp,%eax; movl $NR,%edx; call eax):
     * [ESP]     = thunk_ret (return to thunk's pop ebp; ret, pushed by thunk's call)
     * [ESP+4]   = original EBP (pushed by thunk)
     * [ESP+8]   = guest caller's return address (pushed by guest's call to thunk)
     *
     * EDX = syscall number (set by thunk)
     * EAX = dispatcher address (set by thunk, clobbered by dispatcher work)
     *
     * EBP is handled by thunk push/pop (like RDI in 64-bit) — NOT saved/restored
     * here. EAX is used to carry the dispatcher address in and the syscall
     * result out. We save EDX, ESP, EIP, and remaining guest registers. */

    /* Save EDX (syscall nr) first — we'll reuse EDX as temp below */
    movl %edx, __wine_guest_regs + EDX_OFF

    /* Save EFLAGS onto guest stack (pushf). After this:
     * [ESP]     = saved EFLAGS
     * [ESP+4]   = thunk_ret (pushed by thunk's call eax)
     * [ESP+8]   = original EBP (pushed by thunk)
     * [ESP+12]  = guest caller's return address
     * ESP now points to the saved EFLAGS so we can popf on restore. */
    pushf
    movl %esp, __wine_guest_regs + ESP_OFF

    /* EIP at [ESP+4], EFLAGS at [ESP] — use EDX as temp (already saved) */
    movl 4(%esp), %edx
    movl %edx, __wine_guest_regs + EIP_OFF

    movl (%esp), %edx
    movl %edx, __wine_guest_regs + EFLAGS_OFF

    /* Restore EDX (syscall nr) */
    movl __wine_guest_regs + EDX_OFF, %edx

    /* Save volatile guest registers */
    movl %ecx, __wine_guest_regs + ECX_OFF

    /* Save callee-saved guest registers (except EBP — thunk handles it) */
    movl %ebx, __wine_guest_regs + EBX_OFF
    movl %esi, __wine_guest_regs + ESI_OFF
    movl %edi, __wine_guest_regs + EDI_OFF

    /* ── Switch to UNIX stack ── */

    movl unix_stack_ptr_val, %esp
    subl $8, %esp

    /* ── Call C dispatcher (EDX still has syscall nr) ── */

    pushl %edx
    call c_dispatch_syscall
    addl $4, %esp

    /* ── Save return value (in EAX) ── */

    movl %eax, __wine_guest_regs + EAX_OFF

    /* ── Switch back to guest stack ── */

    movl __wine_guest_regs + ESP_OFF, %esp  /* ESP points to saved EFLAGS on stack */

    /* ── Restore guest registers ── */

    movl __wine_guest_regs + EBX_OFF, %ebx
    movl __wine_guest_regs + ECX_OFF, %ecx
    movl __wine_guest_regs + EDX_OFF, %edx
    movl __wine_guest_regs + ESI_OFF, %esi
    movl __wine_guest_regs + EDI_OFF, %edi
    movl __wine_guest_regs + EAX_OFF, %eax

    /* Restore EFLAGS from guest stack (pushf/popf pair),
     * then ESP += 4 so it points to thunk_ret for the return. */
    popf

    /* EBP is NOT restored here — the thunk's `pop ebp` after our ret handles it.
     * ret: jump to thunk_ret (at ESP), thunk does pop ebp + ret → guest caller.
     * EAX carries the syscall result (restored from guest_regs above). */

    ret

#endif /* __i386__ */
