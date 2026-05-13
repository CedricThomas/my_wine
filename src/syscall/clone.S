/*
 * clone.S — 32-bit clone() syscall wrapper with child fn invocation
 *
 * Implements wine_clone(flags, child_stack, parent_tid, child_tid, fn, arg)
 * as a standalone assembly function for i386.
 *
 * When child_stack is NULL, allocates a 4KB stack via mmap2.
 * Writes fn (as return address) and arg onto the child stack so that
 * after clone returns in the child, popping executes fn(arg).
 * Child then exits via the exit(0) syscall.
 * Parent receives child PID in EAX.
 *
 * Modelled after glibc's sysdeps/unix/sysv/linux/i386/clone.S.
 */

#if defined(__i386__)

#include <asm/unistd_32.h>

/* Mmap constants (from <sys/mman.h>) — defined here to avoid pulling
 * C headers into assembly. Using raw values since we only need mmap2. */
#define PROT_READ  0x01
#define PROT_WRITE 0x02
#define MAP_PRIVATE 0x02
#define MAP_ANONYMOUS 0x20

.section .text
.align 16
.code32

/*
 * int wine_clone(int flags, void *child_stack, int *parent_tid,
 *                int *child_tid, void (*fn)(void *), void *arg)
 *
 * cdecl: all arguments on the stack.
 * Returns: child PID in EAX (positive) or error code (negative).
 */
.globl wine_clone
.type wine_clone, @function

wine_clone:
    /* Save all callee-saved registers (+ EBP).
     * Stack after 4 pushes (ESP shifted by -16):
     *   [ESP+0]  = saved EBP
     *   [ESP+4]  = saved EBX
     *   [ESP+8]  = saved ESI
     *   [ESP+12] = saved EDI
     *   [ESP+16] = return address (from caller's `call wine_clone`)
     *   [ESP+20] = flags        (arg1)
     *   [ESP+24] = child_stack  (arg2)
     *   [ESP+28] = parent_tid   (arg3)
     *   [ESP+32] = child_tid    (arg4)
     *   [ESP+36] = fn           (arg5)
     *   [ESP+40] = arg          (arg6)
     */
    pushl %ebp
    pushl %ebx
    pushl %esi
    pushl %edi

    /* Load syscall arguments into registers */
    movl 20(%esp), %ebx         /* flags → EBX (arg1 for clone) */
    movl 24(%esp), %ecx         /* child_stack → ECX (arg2 for clone) */
    movl 28(%esp), %edx         /* parent_tid → EDX (arg3 for clone) */
    movl 32(%esp), %esi         /* child_tid → ESI (arg4 for clone) */

    /* If child_stack is NULL, allocate 4KB via mmap2 syscall (192).
     * mmap2(addr, len, prot, flags, fd, page_offset)
     * On i386, 6-arg syscalls pass the 6th arg (page_offset) on the
     * stack, 4 bytes below ESP (so ESP points to arg5 = fd).
     * Note: mmap2 clobbers ESP (kernel sets it for 6th arg access),
     * but the 4 pushes above ensure ESP is stable for subsequent loads.
     */
    testl %ecx, %ecx
    jne 1f

    pushl $1                    /* page_offset = 1 (4KB = 1 page) */
    pushl $-1                   /* fd = -1 (anonymous) */
    movl $MAP_PRIVATE | MAP_ANONYMOUS, %ebp  /* flags */
    movl $PROT_READ | PROT_WRITE, %edx       /* prot */
    movl $4096, %ecx            /* len */
    xorl %ebx, %ebx             /* addr = NULL */
    movl $__NR_mmap2, %eax      /* syscall 192 */
    int $0x80
    addl $8, %esp               /* clean up pushed args */

    testl %eax, %eax
    js 2f                       /* mmap2 returned error → jump to parent path */
    movl %eax, %ecx             /* new stack base → child_stack */

1:
    /* Set up the child's initial stack.
     * child_stack points past the top of the allocated region.
     * Align to 16 bytes (stack grows downward), then write:
     *   [new_sp-8] = fn       (will be popped as return address)
     *   [new_sp-4] = arg      (will be loaded as ECX for fn call)
     *
     * Linux clone syscall expects newsp to point to the TOP of the
     * new stack (i.e., the highest valid address). The child's ESP
     * will be set to this value when clone returns in the child.
     */
    andl $-16, %ecx             /* align down to 16 bytes → new_sp */

    subl $4, %ecx               /* new_sp -= 4 */
    movl 40(%esp), %eax         /* temp: load arg via register */
    movl %eax, (%ecx)           /* write arg at [new_sp] */

    subl $4, %ecx               /* new_sp -= 4 */
    movl 36(%esp), %eax         /* temp: load fn via register */
    movl %eax, (%ecx)           /* write fn at [new_sp] (return address for child) */

    /* new_sp now points to fn on the child's stack, with arg just above.
     * After clone returns in the child:
     *   ESP = new_sp → [ESP] = fn, [ESP+4] = arg
     */
    addl $8, %ecx               /* new_sp += 8 → top of child stack (kernel sets ESP here) */

    /* Execute the clone syscall.
     * 32-bit clone: flags(EBX), newsp(ECX), parent_tid(EDX),
     *                child_tid(ESI), tls(ESP → NULL via stack layout)
     *
     * In the child, ESP will be set to new_sp (our aligned stack top + 8).
     * In the parent, EAX will contain the child PID.
     */
    movl 20(%esp), %ebx         /* reload flags (clobbered by mmap2) */
    movl 28(%esp), %edx         /* reload parent_tid (clobbered by mmap2 prot arg) */
    xorl %ebp, %ebp             /* tls = NULL (5th clone arg on i386) */

    movl $__NR_clone, %eax      /* syscall 120 */
    int $0x80

    /* ── Parent path (EAX != 0) ── */
    jne 2f

    /* ── Child path (EAX == 0) ── */
    /*
     * ESP is at new_sp:
     *   [ESP]     = fn   (return address)
     *   [ESP+4]   = arg  (function argument)
     *
     * Pop fn into EAX, call fn(arg) — arg is already on stack at [ESP+4] for cdecl.
     */
    popl %eax                   /* fn ← [ESP], ESP++ */
    call *%eax                  /* call fn(arg) — on return, jump to __child_exit */

__child_exit:
    /* Child exits with code 0 */
    movl $__NR_exit, %eax       /* syscall 1 */
    xorl %ebx, %ebx             /* code = 0 */
    int $0x80
    /* Fallback (should never reach) */
    hlt

    /* ── Parent path (EAX != 0, holds child PID) ── */
2:
    /* Restore saved registers and return.
     * EAX already holds: child PID (positive) or error (negative).
     */
    popl %edi
    popl %esi
    popl %ebx
    popl %ebp

    ret

wine_clone_end:
.size wine_clone, wine_clone_end - wine_clone

#else
#error "clone.S must be compiled with -m32 (__i386__)"
#endif /* __i386__ */
