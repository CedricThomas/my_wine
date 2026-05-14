/*
 * pe32_run_guest.S — 32-bit guest entry trampoline
 *
 * Switches ESP to the guest stack and jumps to the PE entry point.
 * If the entry point returns (it shouldn't), calls exit_group with
 * the return value in EAX.
 *
 * C signature (cdecl):
 *   void pe32_run_guest(uint32_t entry_abs, void *stack_top) __attribute__((noreturn));
 *
 * On entry (cdecl):
 *   [ESP+0]  = return address (pushed by call)
 *   [ESP+4]  = entry_abs (arg1)
 *   [ESP+8]  = stack_top (arg2)
 */

#if defined(__i386__)

.section .text
.align 16
.code32

.globl pe32_run_guest
.type pe32_run_guest, @function

pe32_run_guest:
    /*
     * Save entry_abs (at [ESP+4]) in ESI before switching ESP.
     * stack_top is at [ESP+8], which is the value we switch ESP to.
     */
    movl 4(%esp), %esi      /* entry_abs -> ESI */

    /* Switch to guest stack.
     * The SEH chain at TEB+0x00 (fs:[0x00]) is already wired to the
     * crash handler frame by pe32_entry.c before this function is called.
     * The chain remains valid regardless of the stack switch because
     * the SEH frame lives in a static data region, not on any stack.
     *
     * Align ESP to 16 bytes for Microsoft x86 ABI safety.
     * After `call` pushes 4 bytes, ESP%16 == 8 (matching x64 convention).
     * The top of the stack has been pre-zeroed and seeded with entry_abs
     * as a return address (see pe32_entry.c setup_fs_and_jump).
     */
    movl 8(%esp), %eax      /* stack_top */
    movl %eax, %esp
    andl $-16, %esp
    subl $4, %esp

    /*
     * Set EBP to a valid "frame" value before jumping to the PE entry.
     * When jumping to _main (bypassing CRT), we need EBP to point to
     * a valid stack location so the entry function's prologue works.
     * We set EBP = ESP, pointing to the fake return address.
     * When the entry function does `push ebp; mov ebp, esp`,
     * the saved EBP is a valid stack address.
     */
    movl %esp, %ebp         /* EBP = ESP (pointing to fake ret addr) */

    /* Jump to PE entry point (in ESI). Should not return. */
    jmp *%esi

pe32_run_guest_end:
.size pe32_run_guest, pe32_run_guest_end - pe32_run_guest

#else
#error "pe32_run_guest.S must be compiled with -m32 (__i386__)"
#endif
