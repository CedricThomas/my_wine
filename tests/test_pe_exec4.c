#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <sys/mman.h>

int main(void)
{
    /* Map code page RWX */
    void *code_page = mmap(NULL, 4096, PROT_READ | PROT_WRITE | PROT_EXEC,
                           MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    
    /* Write: ret */
    unsigned char *code = code_page;
    code[0] = 0xC3;  /* ret */
    
    /* Map stack page RW */
    void *stack_page = mmap(NULL, 4096, PROT_READ | PROT_WRITE,
                            MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    uint64_t stack_top = (uint64_t)stack_page + 4096;
    stack_top &= ~(uint64_t)15;
    
    /* Write a return address on the new stack */
    void (*cleanup)(void) = ^(void){}; /* no, can't do blocks in C */
    
    /* Write a valid return address (to the instruction after our asm) */
    extern void after_asm;
    *(void **)(stack_top - 8) = &&after_asm; /* goto label as value */
    
    printf("stack_top=0x%lx\n", stack_top);
    
    __asm__ __volatile__(
        "mov %0, %%rax\n"
        "and $~0xF, %%rax\n"
        "mov %%rax, %%rsp\n"
        "jmp *%1\n"
        :
        : "r"(stack_top), "r"(code_page)
        : "rax", "memory"
    );
    after_asm:
    printf("Returned successfully!\n");
    
    munmap(code_page, 4096);
    munmap(stack_page, 4096);
    return 0;
}
