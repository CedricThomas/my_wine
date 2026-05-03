#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <sys/mman.h>

int main(void)
{
    void *page = mmap(NULL, 4096, PROT_READ | PROT_WRITE | PROT_EXEC,
                      MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    
    if (page == MAP_FAILED) {
        perror("mmap");
        return 1;
    }
    
    printf("mmap succeeded: %p\n", page);
    
    /* Write: xor rax, rax; ret */
    unsigned char *code = page;
    code[0] = 0x48; code[1] = 0x31; code[2] = 0xC0; /* xor rax, rax */
    code[3] = 0xC3;                                    /* ret */
    
    printf("Code written\n");
    
    /* Try to execute via function pointer */
    uint64_t (*fn)(void) = (uint64_t (*)(void))page;
    printf("About to call function at %p\n", page);
    
    uint64_t result = fn();
    printf("Result: %lu\n", result);
    
    munmap(page, 4096);
    return 0;
}
