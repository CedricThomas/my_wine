#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <unistd.h>

#include "test_helpers.h"

/* ── Helper: build a minimal PE DLL on disk with exports ── */
const char *build_dll_on_disk(const char *dll_path,
                              const char **export_names,
                              int num_exports)
{
    /* Create a minimal PE DLL in anonymous memory, then write to disk. */
    size_t buf_size = 0x3000;
    void *base = mmap(NULL, buf_size, PROT_READ | PROT_WRITE,
                      MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (base == MAP_FAILED) {
        perror("mmap");
        return NULL;
    }
    memset(base, 0, buf_size);

    uint8_t *p = (uint8_t *)base;

    /* DOS header */
    IMAGE_DOS_HEADER *dos = (IMAGE_DOS_HEADER *)(p + 0x0000);
    dos->e_magic = IMAGE_DOS_SIGNATURE;
    dos->e_lfanew = 0x80;

    /* NT headers — write as IMAGE_NT_HEADERS64 (raw PE layout for file) */
    IMAGE_NT_HEADERS64 *nt = (IMAGE_NT_HEADERS64 *)(p + 0x0080);
    memset(nt, 0, sizeof(IMAGE_NT_HEADERS64));
    nt->Signature = IMAGE_NT_SIGNATURE;
    nt->FileHeader.Machine = IMAGE_FILE_MACHINE_AMD64;
    nt->FileHeader.NumberOfSections = 2;
    nt->FileHeader.SizeOfOptionalHeader = sizeof(IMAGE_OPTIONAL_HEADER64);
    nt->FileHeader.Characteristics = 0x2000; /* IMAGE_FILE_DLL */
    nt->OptionalHeader.Magic = IMAGE_NT_OPTIONAL_HDR64_MAGIC;
    nt->OptionalHeader.SectionAlignment = 0x1000;
    nt->OptionalHeader.FileAlignment = 0x200;
    nt->OptionalHeader.SizeOfImage = 0x3000;
    nt->OptionalHeader.SizeOfHeaders = 0x1000;
    nt->OptionalHeader.ImageBase = 0; /* let map_image pick any base */
    /* Point export directory into .rdata, no import directory */
    nt->OptionalHeader.DataDirectory[DIRECTORY_ENTRY_EXPORT].VirtualAddress = 0x2000;
    nt->OptionalHeader.DataDirectory[DIRECTORY_ENTRY_EXPORT].Size = 0x200;
    nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT].VirtualAddress = 0;
    nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT].Size = 0;

    /* Section headers */
    size_t sec_off = 0x80 + sizeof(uint32_t) + sizeof(IMAGE_FILE_HEADER)
                    + sizeof(IMAGE_OPTIONAL_HEADER64);
    IMAGE_SECTION_HEADER *sec = (IMAGE_SECTION_HEADER *)(p + sec_off);

    memcpy(sec[0].Name, ".text\0\0\0", 8);
    sec[0].Misc.VirtualSize = 0x1000;
    sec[0].VirtualAddress = 0x1000;
    sec[0].SizeOfRawData = 0x1000;
    sec[0].PointerToRawData = 0x1000;
    sec[0].Characteristics = IMAGE_SCN_MEM_READ | IMAGE_SCN_MEM_EXECUTE;

    memcpy(sec[1].Name, ".rdata\0\0", 8);
    sec[1].Misc.VirtualSize = 0x1000;
    sec[1].VirtualAddress = 0x2000;
    sec[1].SizeOfRawData = 0x1000;
    sec[1].PointerToRawData = 0x2000;
    sec[1].Characteristics = IMAGE_SCN_MEM_READ;

    /* Stub function bytes */
    for (int i = 0; i < num_exports; i++) {
        uint32_t rva = 0x1000 + i * 0x10;
        p[rva] = 0xC3; /* ret */
    }

    /* Export directory at RVA 0x2000 */
    IMAGE_EXPORT_DIRECTORY *exp = (IMAGE_EXPORT_DIRECTORY *)(p + 0x2000);
    exp->NumberOfFunctions = (uint32_t)num_exports;
    exp->NumberOfNames = (uint32_t)num_exports;
    exp->Base = 1;
    exp->Name = 0x2028;

    uint32_t name_table_rva = 0x2030;
    uint32_t ordinal_rva = name_table_rva + num_exports * sizeof(uint32_t);
    uint32_t func_rva = ordinal_rva + num_exports * sizeof(uint16_t);
    uint32_t name_str_rva = func_rva + num_exports * sizeof(uint32_t);

    exp->AddressOfNames = name_table_rva;
    exp->AddressOfNameOrdinals = ordinal_rva;
    exp->AddressOfFunctions = func_rva;

    /* DLL name string */
    memcpy(p + 0x2028, "TDLL.DLL\0", 9);

    /* AddressOfNames */
    uint32_t *names_arr = (uint32_t *)(p + name_table_rva);
    uint32_t cur = name_str_rva;
    for (int i = 0; i < num_exports; i++) {
        names_arr[i] = cur;
        cur += (uint32_t)(strlen(export_names[i]) + 1);
    }

    /* AddressOfNameOrdinals */
    uint16_t *ords = (uint16_t *)(p + ordinal_rva);
    for (int i = 0; i < num_exports; i++) {
        ords[i] = (uint16_t)i;
    }

    /* AddressOfFunctions */
    uint32_t *funcs = (uint32_t *)(p + func_rva);
    for (int i = 0; i < num_exports; i++) {
        funcs[i] = 0x1000 + i * 0x10;
    }

    /* Name strings */
    uint8_t *str_pos = p + name_str_rva;
    for (int i = 0; i < num_exports; i++) {
        memcpy(str_pos, export_names[i], strlen(export_names[i]) + 1);
        str_pos += strlen(export_names[i]) + 1;
    }

    /* Write to disk */
    int fd = open(dll_path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) {
        perror("open");
        munmap(base, buf_size);
        return NULL;
    }
    if (write(fd, base, buf_size) != (ssize_t)buf_size) {
        perror("write");
        close(fd);
        munmap(base, buf_size);
        return NULL;
    }
    close(fd);
    munmap(base, buf_size);
    return dll_path;
}
