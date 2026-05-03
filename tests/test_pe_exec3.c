#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <sys/mman.h>
#include <string.h>

int main(void)
{
    /* Map code page RWX */
    void *code_page = mmap(NULL, 4096, PROT_READ | PROT_WRITE | PROT_EXEC,
                           MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    printf("code_page=%p\n", code_page);
    
    /* Write: sub rsp, 0x28; mov rax, 42; add rsp, 0x28; ret */
    unsigned char *code = code_page;
    int n = 0;
    code[n++] = 0x48; code[n++] = 0x83; code[n++] = 0xEC; code[n++] = 0x28;  /* sub rsp, 0x28 */
    code[n++] = 0x48; code[n++] = 0xC7; code[n++] = 0xC0;                     /* mov rax, imm64 */
    code[n++] = 0x2A; code[n++] = 0x00; code[n++] = 0x00; code[n++] = 0x00;  /* 42 */
    code[n++] = 0x48; code[n++] = 0x83; code[n++] = 0xC4; code[n++] = 0x28;  /* add rsp, 0x28 */
    code[n++] = 0xC3;                                                           /* ret */
    printf("Wrote %d bytes of code\n", n);
    
    /* Map stack page RWX (same protections as code) */
    void *stack_page = mmap(NULL, 4096, PROT_READ | PROT_WRITE | PROT_EXEC,
                            MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    uint64_t stack_top = (uint64_t)stack_page + 4096;
    stack_top &= ~(uint64_t)15;
    printf("stack_top=0x%lx\n", stack_top);
    
    /* Write marker at stack top */
    *(uint64_t*)(stack_top - 8) = 0xDEADBEEF;
    printf("marker written\n");
    
    /* Jump */
    __asm__ __volatile__(
        "mov %0, %%rax\n"
        "and $~0xF, %%rax\n"
        "mov %%rax, %%rsp\n"
        "jmp *%1\n"
        :
        : "r"(stack_top), "r"(code_page)
        : "rax", "memory"
    );
    
    return 0;
}
