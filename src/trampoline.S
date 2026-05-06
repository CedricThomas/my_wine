    .text
    .globl trampoline_jump
    .type trampoline_jump, @function

trampoline_jump:
    mov %rdi, %r8          /* r8 = new_stack_top */
    mov %rsi, %r9          /* r9 = peb */
    mov %rdx, %r10         /* r10 = entry_point */
    
    /* Switch to new stack */
    mov %r8, %rsp
    and $~0xF, %rsp
    
    /* Write marker to verify stack is writable */
    movl $0xDEADBEEF, (%rsp)
    
    /* Set up Windows x64 calling convention */
    mov %r9, %rcx          /* RCX = PEB */
    xor %rdx, %rdx         /* RDX = 0 */
    
    /* First try: read from the PE's .rdata to check if .rdata is readable */
    movabs $0x140004460, %r11  /* .refptr.mingw_app_type address */
    mov (%r11), %r12          /* Read the refptr value */
    
    /* Write the value to a known location on the stack for debugging */
    mov %r12, 8(%rsp)
    
    /* Jump to entry point */
    jmp *%r10
    
    .size trampoline_jump, .-trampoline_jump
