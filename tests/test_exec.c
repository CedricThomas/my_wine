#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <sys/mman.h>
#include <string.h>

int main(void)
{
    /* Map a page with RX (like .text section) */
    void *page = mmap(NULL, 4096, PROT_READ | PROT_EXEC,
                      MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    
    /* Write a simple function: mov rax, 42; ret */
    unsigned char *code = page;
    code[0] = 0x48;  /* rex.W */
    code[1] = 0xC7;  /* mov rax, imm64 */
    code[2] = 0xC0;
    code[3] = 0x2A;  /* 42 */
    code[4] = 0x00;
    code[5] = 0x00;
    code[6] = 0x00;
    code[7] = 0x00;
    code[8] = 0xC3;  /* ret */
    
    /* Call it as a function pointer */
    uint64_t (*fn)(void) = (uint64_t (*)(void))page;
    uint64_t result = fn();
    printf("Result: %lu (expected 42)\n", result);
    
    munmap(page, 4096);
    return result == 42 ? 0 : 1;
}
