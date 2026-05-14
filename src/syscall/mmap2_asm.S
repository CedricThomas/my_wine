/*
 * mmap2_asm.S — 32-bit mmap2 syscall wrapper
 *
 * Workaround for 32-bit builds: musl's syscall() function dispatches
 * via gs:[0x10], but our custom _start never initializes gs. So we
 * need a direct int $0x80 wrapper for mmap2 that handles the ebp
 * (page_offset) argument without fighting GCC's frame pointer.
 *
 * This is a standalone assembly function compiled with -m32.
 * Declared in syscalls_inline.h as a fallback for 32-bit mmap2.
 */

#if defined(__i386__)

#include <asm/unistd_32.h>

.section .text
.align 16
.code32

/*
 * void *synct_mmap2(void *addr, size_t len, int prot,
 *                   int flags, int fd, off_t offset)
 *
 * Performs the mmap2 syscall directly via int $0x80.
 * Arguments in cdecl calling convention (on stack):
 *   [ESP+4]  return address
 *   [ESP+8]  addr
 *   [ESP+12] len
 *   [ESP+16] prot
 *   [ESP+20] flags
 *   [ESP+24] fd
 *   [ESP+28] offset (byte offset)
 *
 * Returns: mapped address in EAX (or error code if < 0)
 */
.globl synct_mmap2
.type synct_mmap2, @function

synct_mmap2:
    /*
     * Save all caller-saved regs + ebp (frame pointer) since
     * we're about to clobber everything for the syscall.
     */
    pushl %ebp
    pushl %ebx
    pushl %esi
    pushl %edi

    /* Load syscall arguments into registers
     * Stack after 4 pushes:
     *   [esp+0]  = saved ebp
     *   [esp+4]  = saved ebx
     *   [esp+8]  = saved esi
     *   [esp+12] = saved edi
     *   [esp+16] = return address (pushed by call)
     *   [esp+20] = arg1 (addr)
     *   [esp+24] = arg2 (len)
     *   [esp+28] = arg3 (prot)
     *   [esp+32] = arg4 (flags)
     *   [esp+36] = arg5 (fd)
     *   [esp+40] = arg6 (offset)
     */
    movl $__NR_mmap2, %eax        /* syscall number (immediate) */
    movl 20(%esp), %ebx           /* addr (arg1) */
    movl 24(%esp), %ecx           /* len  (arg2) */
    movl 28(%esp), %edx           /* prot (arg3) */
    movl 32(%esp), %esi           /* flags(arg4) */
    movl 36(%esp), %edi           /* fd   (arg5) */
    movl 40(%esp), %ebp           /* offset (arg6) - page offset in ebp */

    /* Convert byte offset to page offset (shift right 12) */
    shrl $12, %ebp

    /* Execute syscall */
    int $0x80

    /* Restore saved registers */
    popl %edi
    popl %esi
    popl %ebx
    popl %ebp

    ret

synct_mmap2_end:
.size synct_mmap2, synct_mmap2_end - synct_mmap2

#else
#error "mmap2_asm.S must be compiled with -m32 (__i386__)"
#endif /* __i386__ */
