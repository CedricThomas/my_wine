/*
 * pe32_entry.S — 32-bit ELF entry point for my_wine_32
 *
 * This is the _start symbol for a statically-linked 32-bit ELF binary.
 * Compiled with -m32. Aligns the stack to 16 bytes (required by
 * glibc on i386), calls wine32_main, then exits via exit_group.
 *
 * On x86-64 hosts, the kernel passes 64-bit argv/envp pointers on the
 * stack even for 32-bit binaries. We read the lower 32 bits which work
 * since all strings are below 4GB. We also build a fake argv array on
 * the aligned stack so C code gets a usable argv[1].
 */

#if defined(__i386__)

.text
.align 16
.code32

.extern environ
.globl _start
_start:
    /*
     * Save argc and first two argv pointers from the kernel stack BEFORE
     * we break alignment.
     * Stack layout: [ESP]=argc, [ESP+4]=argv[0], [ESP+8]=argv[1] or 0
     */
    movl (%esp), %ecx        /* ECX = argc */
    movl 4(%esp), %edx       /* EDX = argv[0] pointer */
    movl 8(%esp), %eax       /* EAX = argv[1] pointer (or 0 if argc == 1) */

    /*
     * Align stack to 16 bytes. Save orig_ESP so we can later find
     * envp on the original kernel stack.
     */
    movl %esp, %esi        /* ESI = orig_ESP (reuse for scan later) */
    andl $-16, %esp        /* ESP aligned down to 16-byte boundary */
    subl $4, %esp          /* ESP = aligned - 4; after call will be 16-byte */

    /*
     * Initialize the global `environ` variable.
     *
     * Scan the 32-bit argv pointers on the original kernel stack to find
     * the NULL terminator — the next slot is envp[0].
     */
    subl %esp, %esi        /* ESI = orig_ESP - ESP_before_call (shift) */
    leal 4(%esp,%esi), %esi    /* ESI = orig_ESP + 4 = argv[0] slot */
1:
    addl $4, %esi              /* advance to next 32-bit pointer slot (kernel pushes 4-byte ptrs) */
    cmpl $0, (%esi)            /* is this pointer NULL (end of argv)? */
    jne 1b                     /* no — keep scanning */
    addl $4, %esi              /* past NULL terminator = envp array start */
    movl %esi, environ         /* store envp pointer in global environ */

    /*
     * Build a fake argv array on the aligned stack and push it for
     * the cdecl call. This gives wine32_main a working argv[1].
     *
     * Stack after pushes (top to bottom):
     *   [ESP]       = 0 (NULL terminator)
     *   [ESP+4]     = argv[1] pointer
     *   [ESP+8]     = argv[0] pointer
     *   [ESP+12]    = argc (pushed below)
     *   [ESP+16]    = &fake_argv (pushed below)
     *
     * After `call` pushes return address:
     *   [ESP]       = return address
     *   [ESP+4]     = &fake_argv (C param: argv)
     *   [ESP+8]     = argc (C param: argc)
     */
    pushl $0                 /* fake argv array: NULL terminator */
    pushl %eax               /* fake argv array: argv[1] */
    pushl %edx               /* fake argv array: argv[0] */
    movl %esp, %edx          /* EDX = &fake_argv array */
    pushl %edx               /* push &fake_argv (will be at [ESP+4] after call) */
    pushl %ecx               /* push argc (will be at [ESP+8] after call) */

    /* Call the C entry point. wine32_main never returns — it jumps
     * to the PE entry point (or exits via syscall). */
    call wine32_main

    /*
     * Should never reach here. Exit with return code in EAX.
     * __NR_exit_group (231) on i386 terminates all threads.
     */
    xorl %ebx, %ebx
    movl %eax, %ebx       /* return code as exit status */
    movl $231, %eax      /* __NR_exit_group */
    int $0x80

#else
#error "pe32_entry.S must be compiled with -m32 (__i386__)"
#endif /* __i386__ */
