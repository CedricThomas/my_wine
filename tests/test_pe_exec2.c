#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <sys/mman.h>
#include <string.h>

int main(void)
{
    /* Map a page RWX */
    void *page = mmap(NULL, 4096, PROT_READ | PROT_WRITE | PROT_EXEC,
                      MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    printf("page=%p\n", page);
    
    /* Write PE-like code: sub rsp, 0x28; mov rax, 42; add rsp, 0x28; ret */
    unsigned char *code = page;
    int n = 0;
    code[n++] = 0x48; code[n++] = 0x83; code[n++] = 0xEC; code[n++] = 0x28;  /* sub rsp, 0x28 */
    code[n++] = 0x48; code[n++] = 0xC7; code[n++] = 0xC0;                     /* mov rax, imm64 */
    code[n++] = 0x2A; code[n++] = 0x00; code[n++] = 0x00; code[n++] = 0x00;  /* 42 */
    code[n++] = 0x48; code[n++] = 0x83; code[n++] = 0xC4; code[n++] = 0x28;  /* add rsp, 0x28 */
    code[n++] = 0xC3;                                                           /* ret */
    printf("Wrote %d bytes of code\n", n);
    
    /* New stack */
    char stack[65536] __attribute__((aligned(16)));
    uint64_t stack_top = (uint64_t)stack + sizeof(stack);
    stack_top &= ~(uint64_t)15;
    printf("stack_top=0x%lx\n", stack_top);
    
    /* Write marker at stack top to verify */
    *(uint64_t*)(stack_top - 8) = 0xDEADBEEF;
    
    /* Jump */
    __asm__ __volatile__(
        "mov %0, %%rax\n"
        "and $~0xF, %%rax\n"
        "mov %%rax, %%rsp\n"
        "jmp *%1\n"
        :
        : "r"(stack_top), "r"(page)
        : "rax", "memory"
    );
    
    return 0;
}
