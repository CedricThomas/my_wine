/*
 * clone64.S — 64-bit clone() syscall wrapper with child fn invocation
 *
 * Implements wine_clone(flags, child_stack, parent_tid, child_tid, fn, arg)
 * as a standalone assembly function for x86_64 using the System V AMD64 ABI.
 *
 * When child_stack is NULL, allocates a 4KB stack via mmap syscall.
 * Writes fn (as return address) and arg onto the child stack so that
 * after clone returns in the child, popping executes fn(arg).
 * Child then exits via the exit(0) syscall.
 * Parent receives child PID in RAX.
 *
 * Modelled after glibc's sysdeps/unix/sysv/linux/x86_64/clone.S.
 *
 * Caller-saved registers: RAX, RDI, RSI, RBP, R12-R15.
 * Input (System V AMD64 ABI): flags=%rdi, child_stack=%rsi,
 *   parent_tid=%rdx, child_tid=%rcx, fn=%r8, arg=%r9.
 *
 * The -mno-red-zone flag is assumed so no red zone concerns exist.
 */

#if defined(__x86_64__)

#include <asm/unistd_64.h>

/* Mmap constants (from <sys/mman.h>) — defined here to avoid pulling
 * C headers into assembly. */
#define PROT_READ       0x01
#define PROT_WRITE      0x02
#define MAP_PRIVATE     0x02
#define MAP_ANONYMOUS   0x20

.section .text
.align 16
.code64

/*
 * int wine_clone(int flags, void *child_stack, int *parent_tid,
 *                int *child_tid, void (*fn)(void *), void *arg)
 *
 * System V AMD64 ABI:
 *   %rdi = flags,    %rsi = child_stack,  %rdx = parent_tid
 *   %rcx = child_tid, %r8 = fn,           %r9 = arg
 *
 * Returns: child PID in %rax (positive) or error code (negative).
 */
.globl wine_clone
.type wine_clone, @function

wine_clone:
    /* Save all caller-saved registers + all 6 arguments onto the stack.
     * After these 11 pushes:
     *   [rsp+ 0] = saved RBP
     *   [rsp+ 8] = saved R15
     *   [rsp+16] = saved R14
     *   [rsp+24] = saved R13
     *   [rsp+32] = saved R12
     *   [rsp+40] = saved RCX  (child_tid)
     *   [rsp+48] = saved RDX  (parent_tid)
     *   [rsp+56] = saved RSI  (child_stack)
     *   [rsp+64] = saved RDI  (flags)
     *   [rsp+72] = saved R8   (fn)
     *   [rsp+80] = saved R9   (arg)
     */
    push %rbp
    push %r15
    push %r14
    push %r13
    push %r12
    push %rcx
    push %rdx
    push %rsi
    push %rdi
    push %r8
    push %r9

    /* ── Determine child_stack ──
     * If child_stack is NULL, allocate 4KB via mmap syscall (9).
     * mmap: rdi=addr, rsi=len, rdx=prot, r10=flags, r9=fd, r8=offset
     */
    mov 56(%rsp), %rsi          /* child_stack */
    test %rsi, %rsi
    jne 1f

    xor %rdi, %rdi                  /* addr = NULL */
    mov $4096, %rsi                /* len = 4KB */
    mov $PROT_READ | PROT_WRITE, %rdx   /* prot */
    mov $MAP_PRIVATE | MAP_ANONYMOUS, %r10  /* flags */
    mov $-1, %r9                   /* fd = -1 */
    xor %r8, %r8                   /* offset = 0 */
    mov $__NR_mmap, %rax          /* syscall 9 */
    syscall

    test %rax, %rax
    js 2f                         /* mmap returned error → parent path */
    mov %rax, %rsi                /* new stack base → child_stack */

1:
    /* ── Build child stack frame ──
     * Align to 16 bytes (stack grows downward), then write:
     *   [new_sp-8]  = fn       (return address child pops)
     *   [new_sp-16] = arg      (first arg for fn, goes to RDI)
     *
     * The kernel sets child RSP to the value we pass as child_stack
     * (newsp), so we set newsp = new_sp+16 (top of written data).
     * When child runs: [RSP]=fn, [RSP+8]=arg.
     */
    and $-16, %rsi                /* align down to 16 bytes → new_sp */
    mov %rsi, %rax                /* new_sp copy for arithmetic */

    sub $8, %rax
    mov 72(%rsp), %rdx            /* fn */
    mov %rdx, (%rax)              /* [new_sp-8] = fn */

    sub $8, %rax
    mov 80(%rsp), %rdx            /* arg */
    mov %rdx, (%rax)              /* [new_sp-16] = arg */

    add $16, %rsi                 /* new_sp += 16 → top (newsp for kernel) */

    /* ── Execute clone syscall (number 56 on x86_64) ──
     * rax=nr, rdi=flags, rsi=child_stack, rdx=parent_tid,
     * r10=child_tid, r8=NULL(tls)
     */
    mov 64(%rsp), %rdi            /* flags */
    mov 48(%rsp), %rdx            /* parent_tid */
    mov 40(%rsp), %r10            /* child_tid */
    xor %r8, %r8                  /* tls = NULL */
    mov $__NR_clone, %rax         /* syscall 56 */
    syscall

    /* ── Parent path (RAX != 0, holds child PID) ── */
    test %rax, %rax
    jne 2f

    /* ── Child path (RAX == 0) ──
     * RSP points to top of our written stack:
     *   [RSP]     = fn   (return address)
     *   [RSP+8]   = arg
     *
     * Pop fn into R10, set RDI=arg, then call fn.
     * On return from fn, jump to __child_exit.
     */
    mov (%rsp), %r10              /* fn ← [RSP] */
    mov 8(%rsp), %rdi             /* arg → RDI (1st arg for fn) */
    call *%r10                    /* fn(arg) */

    /* Child exits with code 0 */
__child_exit:
    mov $__NR_exit, %rax          /* syscall 60 */
    xor %rdi, %rdi                /* code = 0 */
    syscall
    /* Fallback (should never reach) */
    hlt

    /* ── Parent path (RAX != 0, holds child PID) ── */
2:
    /* Restore saved registers in reverse order of push.
     * RAX already holds: child PID (positive) or error (negative).
     */
    pop %r9
    pop %r8
    pop %rdi
    pop %rsi
    pop %rdx
    pop %rcx
    pop %r12
    pop %r13
    pop %r14
    pop %r15
    pop %rbp

    ret

wine_clone_end:
.size wine_clone, wine_clone_end - wine_clone

#else
#error "clone64.S must be compiled with __x86_64__ defined"
#endif /* __x86_64__ */
