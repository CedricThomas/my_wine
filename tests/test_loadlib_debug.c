#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>

#include "pe.h"
#include "nt_constants.h"
#include "src/loader/module_list.h"
#include "src/loader/peb_ldr.h"
#include "include/kernel32.h"
#include "include/common.h"

void *setup_teb_peb(void);
extern void *g_image_base;

int find_dll_path(const char *dll_name, char *path, size_t path_size);
loaded_module_t *load_dll(const char *path, int depth);
void init_import_table(void);
void init_msvcrt_imports(void);

static int build_dll(void) {
    size_t buf_size = 0x3000;
    void *base = mmap(NULL, buf_size, PROT_READ | PROT_WRITE,
                      MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (base == MAP_FAILED) return 0;
    memset(base, 0, buf_size);

    uint8_t *p = (uint8_t *)base;
    IMAGE_DOS_HEADER *dos = (IMAGE_DOS_HEADER *)(p + 0x0000);
    dos->e_magic = IMAGE_DOS_SIGNATURE;
    dos->e_lfanew = 0x80;

    /* Write as IMAGE_NT_HEADERS64 (raw PE layout for file) */
    IMAGE_NT_HEADERS64 *nt = (IMAGE_NT_HEADERS64 *)(p + 0x0080);
    memset(nt, 0, sizeof(IMAGE_NT_HEADERS64));
    nt->Signature = IMAGE_NT_SIGNATURE;
    nt->FileHeader.Machine = IMAGE_FILE_MACHINE_AMD64;
    nt->FileHeader.NumberOfSections = 2;
    nt->FileHeader.SizeOfOptionalHeader = sizeof(IMAGE_OPTIONAL_HEADER64);
    nt->FileHeader.Characteristics = 0x2000;
    nt->OptionalHeader.Magic = IMAGE_NT_OPTIONAL_HDR64_MAGIC;
    nt->OptionalHeader.SectionAlignment = 0x1000;
    nt->OptionalHeader.FileAlignment = 0x200;
    nt->OptionalHeader.SizeOfImage = 0x3000;
    nt->OptionalHeader.SizeOfHeaders = 0x1000;
    nt->OptionalHeader.ImageBase = 0;
    nt->OptionalHeader.DataDirectory[DIRECTORY_ENTRY_EXPORT].VirtualAddress = 0x2000;
    nt->OptionalHeader.DataDirectory[DIRECTORY_ENTRY_EXPORT].Size = 0x200;

    int fd = open("/tmp/tdll.dll", O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) { munmap(base, buf_size); return 0; }
    if (write(fd, base, buf_size) != (ssize_t)buf_size) { close(fd); munmap(base, buf_size); return 0; }
    close(fd);
    munmap(base, buf_size);
    return 1;
}

int main(void) {
    printf("=== Debug LoadLibraryA ===\n\n");
    
    if (!build_dll()) {
        printf("FAIL: build_dll\n");
        return 1;
    }
    printf("OK: /tmp/tdll.dll created\n");
    
    // Verify it exists with open
    {
        int fd = open("/tmp/tdll.dll", O_RDONLY);
        printf("OK: /tmp/tdll.dll open fd=%d\n", fd);
        if (fd >= 0) close(fd);
    }
    
    printf("g_wine_dll_path before: '%s'\n", g_wine_dll_path);
    set_wine_dll_path("/tmp");
    printf("g_wine_dll_path after:  '%s'\n", g_wine_dll_path);
    
    init_msvcrt_imports();
    init_import_table();
    init_module_list();
    init_peb_ldr();
    
    // Try find_dll_path
    char path[512];
    int rc = find_dll_path("tdll.dll", path, sizeof(path));
    printf("find_dll_path('tdll.dll') = %d, path='%s'\n", rc, path);
    
    if (rc) {
        // Try load_dll directly
        printf("Trying load_dll...\n");
        loaded_module_t *mod = load_dll(path, 0);
        printf("load_dll returned: %p\n", (void *)mod);
        
        if (mod) {
            printf("OK: loaded!\n");
        } else {
            printf("FAIL: load_dll returned NULL\n");
        }
    }
    
    // Now try LoadLibraryA
    printf("\nTrying _LoadLibraryA('tdll.dll')...\n");
    void *base = _LoadLibraryA("tdll.dll");
    printf("_LoadLibraryA returned: %p\n", base);
    
    unlink("/tmp/tdll.dll");
    return base == NULL ? 1 : 0;
}
