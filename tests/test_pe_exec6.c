#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <sys/mman.h>

int main(void)
{
    /* Map code page RWX */
    void *code_page = mmap(NULL, 4096, PROT_READ | PROT_WRITE | PROT_EXEC,
                           MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    
    /* Write: mov rax, 42; hlt (stop to verify we got here) */
    unsigned char *code = code_page;
    int n = 0;
    code[n++] = 0x48; code[n++] = 0xC7; code[n++] = 0xC0;  /* mov rax, imm64 */
    code[n++] = 0x2A; code[n++] = 0x00; code[n++] = 0x00; code[n++] = 0x00;  /* 42 */
    code[n++] = 0xF4;  /* hlt */
    
    /* Map stack page RW */  
    void *stack_page = mmap(NULL, 4096, PROT_READ | PROT_WRITE,
                            MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    uint64_t stack_top = (uint64_t)stack_page + 4096;
    stack_top &= ~(uint64_t)15;
    
    printf("code_page=%p, stack_top=0x%lx\n", code_page, stack_top);
    
    /* Just switch stack and jump - use jmp not call */
    __asm__ __volatile__(
        "mov %0, %%rsp\n"
        "jmp *%1\n"
        :
        : "r"(stack_top), "r"(code_page)
        : "memory"
    );
    
    printf("Should not reach here\n");
    return 0;
}
