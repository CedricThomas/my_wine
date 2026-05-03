#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <sys/mman.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>

int main(int argc, char *argv[])
{
    if (argc < 2) { fprintf(stderr, "Usage: %s <pe_file>\n", argv[0]); return 1; }
    
    /* Map the PE file */
    int fd = open(argv[1], O_RDONLY);
    struct stat st;
    fstat(fd, &st);
    void *file = mmap(NULL, st.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
    close(fd);
    
    /* Parse minimal headers */
    uint16_t machine = *(uint16_t *)((char*)file + 0x38 + 2);
    uint32_t entry_rva = *(uint32_t *)((char*)file + 0x38 + 16);
    uint64_t image_base = *(uint64_t *)((char*)file + 0x38 + 24 + 56);
    uint32_t image_size = *(uint32_t *)((char*)file + 0x38 + 24 + 56 + 8);
    
    printf("entry_rva=0x%x image_base=0x%lx image_size=0x%x\n",
           entry_rva, (unsigned long)image_base, image_size);
    
    /* Map image at fixed address (RWX) */
    void *base = mmap((void*)(uintptr_t)image_base, image_size,
                       PROT_READ | PROT_WRITE | PROT_EXEC,
                       MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED, -1, 0);
    if (base == MAP_FAILED) {
        base = mmap(NULL, image_size,
                     PROT_READ | PROT_WRITE | PROT_EXEC,
                     MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    }
    printf("base=%p\n", base);
    
    /* Copy .text section (section 0) */
    uint32_t text_rva = *(uint32_t *)((char*)file + 0x38 + 96 + 0*40 + 12);
    uint32_t text_size_raw = *(uint32_t *)((char*)file + 0x38 + 96 + 0*40 + 16);
    uint32_t text_ptr_raw = *(uint32_t *)((char*)file + 0x38 + 96 + 0*40 + 20);
    memcpy((char*)base + text_rva, (char*)file + text_ptr_raw, text_size_raw);
    printf("Copied .text: rva=0x%x size=0x%x\n", text_rva, text_size_raw);
    
    munmap(file, st.st_size);
    
    /* Now try to execute the entry point */
    uint64_t entry = (uint64_t)(uintptr_t)base + entry_rva;
    printf("entry=0x%lx\n", entry);
    
    /* Read first few bytes */
    unsigned char *b = (unsigned char*)entry;
    printf("entry bytes: %02x %02x %02x %02x\n", b[0], b[1], b[2], b[3]);
    
    /* Try to execute via function pointer with our stack */
    char stack[65536] __attribute__((aligned(16)));
    uint64_t stack_top = (uint64_t)stack + sizeof(stack);
    stack_top &= ~(uint64_t)15;
    
    printf("stack_top=0x%lx\n", stack_top);
    
    /* Jump with inline asm */
    __asm__ __volatile__(
        "mov %0, %%rax\n"
        "and $~0xF, %%rax\n"
        "mov %%rax, %%rsp\n"
        "jmp *%1\n"
        :
        : "r"(stack_top), "r"(entry)
        : "rax", "memory"
    );
    
    return 0;
}
