#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <sys/mman.h>

int main(void)
{
    /* Map code page RWX */
    void *code_page = mmap(NULL, 4096, PROT_READ | PROT_WRITE | PROT_EXEC,
                           MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    
    /* Write: mov rax, 42; ret */
    unsigned char *code = code_page;
    code[0] = 0x48; code[1] = 0xC7; code[2] = 0xC0;
    code[3] = 0x2A; code[4] = 0x00; code[5] = 0x00; code[6] = 0x00;
    code[7] = 0xC3;
    
    printf("code_page=%p\n", code_page);
    
    /* Execute WITHOUT changing stack - use call */
    uint64_t result = ((uint64_t (*)(void))code_page)();
    printf("Result: %lu\n", result);
    
    munmap(code_page, 4096);
    return result == 42 ? 0 : 1;
}
