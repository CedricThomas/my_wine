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
    
    /* Write a return address on the new stack (points to here) */
    void *ret_addr = &&after_asm;
    *(void **)(stack_top - 8) = ret_addr;
    
    printf("stack_top=0x%lx, ret_addr=%p\n", stack_top, ret_addr);
    
    __asm__ __volatile__(
        "mov %0, %%rax\n"
        "and $~0xF, %%rax\n"
        "mov %%rax, %%rsp\n"
        "call *%1\n"
        "jmp %2\n"
        :
        : "r"(stack_top), "r"(code_page), "r"(ret_addr)
        : "rax", "memory"
    );
    after_asm:
    printf("Returned successfully!\n");
    
    munmap(code_page, 4096);
    munmap(stack_page, 4096);
    return 0;
}
