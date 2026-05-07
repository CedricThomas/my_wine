/*
 * image_mapper.c — PE image mapping
 *
 * Opens a PE file, maps it to the preferred base address, copies
 * section data, sets per-section memory protections, and cleans up.
 */

#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <fcntl.h>

#include "../syscall/syscalls_inline.h"
#include "include/pe.h"
#include "include/nt_constants.h"
#include "include/debug.h"
#include "include/pe_priv.h"
#include "loader_priv.h"
#include "include/common.h"

void *g_image_base = NULL;
static char g_pe_path[512] = {0};
uintptr_t g_host_gs_base = 0;  /* Saved before GS→TEB for unix stack calls */

/**
 * Internal core: map a PE file at the given desired base address.
 *
 * When desired_base is non-zero, maps at that address instead of the
 * PE's preferred ImageBase.  This is used by load_dll() to keep DLLs
 * below 4 GB and avoid the GCC ms_abi 32-bit return truncation bug.
 *
 * Opens the file, maps read-only, parses headers, maps the image
 * memory, copies sections, sets per-section protections, cleans up.
 *
 * @param  path         path to the PE file
 * @param  out_dos      (optional) receives parsed DOS header
 * @param  out_nt       (optional) receives parsed NT headers
 * @param  out_nt_size  (optional) receives size of parsed NT headers struct
 * @param  desired_base forced image base (0 = use PE's preferred ImageBase)
 * @return  image base address (virtual), or NULL on failure
 */
void *map_image_at(const char *path,
                   IMAGE_DOS_HEADER *out_dos,
                   IMAGE_NT_HEADERS64 *out_nt,
                   size_t *out_nt_size,
                   uintptr_t desired_base)
{
    /* Save PE path for DLL search — hand-rolled copy, no glibc */
    {
        size_t i;
        for (i = 0; path[i] && i < sizeof(g_pe_path) - 1; i++)
            g_pe_path[i] = path[i];
        g_pe_path[i] = '\0';
    }

    /* 1. Open the PE file */
    long fd = INLINE_SYSCALL_OPENAT(AT_FDCWD, path, O_RDONLY);
    if (fd < 0) { DEBUG("open failed"); return NULL; }

    struct stat st;
    if (INLINE_SYSCALL_FSTAT(fd, &st) < 0) { DEBUG("fstat failed"); INLINE_SYSCALL_CLOSE(fd); return NULL; }
    size_t file_size = (size_t)st.st_size;

    /* 2. Map file read-only */
    void *file_base = INLINE_SYSCALL_MMAP(NULL, file_size, PROT_READ, MAP_PRIVATE, fd, 0);
    if (file_base == MAP_FAILED) { DEBUG("mmap file failed"); INLINE_SYSCALL_CLOSE(fd); return NULL; }

    /* 3. Parse headers */
    IMAGE_DOS_HEADER dos;
    if (parse_dos_header(file_base, file_size, &dos) != 0) {
        DEBUG("Invalid DOS header");
        INLINE_SYSCALL_MUNMAP(file_base, file_size);
        INLINE_SYSCALL_CLOSE(fd);
        return NULL;
    }

    IMAGE_NT_HEADERS64 nt;
    int parse_result = parse_nt_headers(file_base, file_size, &dos, &nt);
    if (parse_result == -2) {
        /* PE32 (32-bit) detected — error already printed by parse_nt_headers */
        INLINE_SYSCALL_MUNMAP(file_base, file_size);
        INLINE_SYSCALL_CLOSE(fd);
        return NULL;
    }
    if (parse_result != 0) {
        DEBUG("Invalid NT headers");
        INLINE_SYSCALL_MUNMAP(file_base, file_size);
        INLINE_SYSCALL_CLOSE(fd);
        return NULL;
    }

    IMAGE_SECTION_HEADER *sections = NULL;
    int num_sections = parse_sections(file_base, file_size, &nt, &sections);
    if (num_sections < 0) {
        DEBUG("Failed to parse sections");
        INLINE_SYSCALL_MUNMAP(file_base, file_size);
        INLINE_SYSCALL_CLOSE(fd);
        return NULL;
    }

    dump_headers(&dos, &nt, sections);

    /* Force flush before debug output */
    fflush(stdout);

    /* Return parsed headers to caller (if requested) */
    if (out_dos) {
        *out_dos = dos;
    }
    if (out_nt) {
        *out_nt = nt;
        if (out_nt_size) {
            *out_nt_size = sizeof(uint32_t) + sizeof(IMAGE_FILE_HEADER) +
                           nt.FileHeader.SizeOfOptionalHeader;
        }
    }

    /* 4. Map image */
    size_t image_size = nt.OptionalHeader.SizeOfImage;

    /* Use desired_base if non-zero, otherwise use the PE's preferred ImageBase */
    uint64_t image_base = (desired_base != 0) ? desired_base : nt.OptionalHeader.ImageBase;

    void *base = INLINE_SYSCALL_MMAP((void *)(uintptr_t)image_base, image_size,
                       PROT_READ|PROT_WRITE|PROT_EXEC,
                       MAP_PRIVATE|MAP_ANONYMOUS|MAP_FIXED, -1, 0);
    if (base == MAP_FAILED) {

        base = INLINE_SYSCALL_MMAP(NULL, image_size,
                     PROT_READ|PROT_WRITE|PROT_EXEC,
                     MAP_PRIVATE|MAP_ANONYMOUS|MAP_STACK, -1, 0);
        if (base == MAP_FAILED) {
            DEBUG("mmap image failed");
            INLINE_SYSCALL_MUNMAP(file_base, file_size);
            INLINE_SYSCALL_CLOSE(fd);
            return NULL;
        }
    }

    /* 5. Copy section data from file to image */
    for (int i = 0; i < num_sections; i++) {
        if (sections[i].SizeOfRawData == 0)
            continue; /* .bss etc. - zero-filled, already anonymous */
        void *dest = (char *)base + sections[i].VirtualAddress;
        void *src  = (char *)file_base + sections[i].PointerToRawData;
        memcpy(dest, src, sections[i].SizeOfRawData);
    }

    /* Copy PE file headers (DOS + NT + section table) into the image */
    {
        uint32_t headers_size = nt.OptionalHeader.SizeOfHeaders;
        if (headers_size > 0) {
            memcpy(base, file_base, headers_size);
            /* Re-point sections into the image */
            sections = get_image_sections(base, &nt);
        }
    }

    /* Apply base relocations (needed when actual base != preferred ImageBase) */
    if (apply_relocations(base, &nt) != 0) {
        DEBUG("Failed to apply relocations");
        INLINE_SYSCALL_MUNMAP(base, image_size);
        INLINE_SYSCALL_MUNMAP(file_base, file_size);
        INLINE_SYSCALL_CLOSE(fd);
        return NULL;
    }

    /* 6. Set per-section protections */
    for (int i = 0; i < num_sections; i++) {
        if (sections[i].Misc.VirtualSize == 0 && sections[i].SizeOfRawData == 0)
            continue;

        int prot = 0;
        if (sections[i].Characteristics & IMAGE_SCN_MEM_READ)
            prot |= PROT_READ;
        if (sections[i].Characteristics & IMAGE_SCN_MEM_WRITE)
            prot |= PROT_WRITE;
        if (sections[i].Characteristics & IMAGE_SCN_MEM_EXECUTE)
            prot |= PROT_EXEC;

        size_t size = sections[i].Misc.VirtualSize;
        if (size == 0)
            size = sections[i].SizeOfRawData;
        size = (size + PAGE_MASK) & ~(size_t)PAGE_MASK;

        if (INLINE_SYSCALL_MPROTECT((char *)base + sections[i].VirtualAddress, size, prot) != 0) {
            DEBUG("mprotect failed");
            INLINE_SYSCALL_MUNMAP(base, image_size);
            INLINE_SYSCALL_MUNMAP(file_base, file_size);
            INLINE_SYSCALL_CLOSE(fd);
            return NULL;
        }
    }

    /* Unmap the original file mapping (no longer needed) */
    INLINE_SYSCALL_MUNMAP(file_base, file_size);
    INLINE_SYSCALL_CLOSE(fd);

    /* Save the image base for later use (import resolution, TEB/PEB, etc.) */
    g_image_base = base;
    return base;
}

/**
 * Map a PE file at the preferred image base (uses PE's ImageBase).
 *
 * Opens the file, maps read-only, parses headers, maps the image
 * memory, copies sections, sets per-section protections, cleans up.
 *
 * @param  path  path to the PE file
 * @param  out_dos   (optional) receives parsed DOS header
 * @param  out_nt    (optional) receives parsed NT headers
 * @param  out_nt_size (optional) receives size of parsed NT headers struct
 * @return  image base address (virtual), or NULL on failure
 */
void *map_image(const char *path,
                IMAGE_DOS_HEADER *out_dos,
                IMAGE_NT_HEADERS64 *out_nt,
                size_t *out_nt_size)
{
    return map_image_at(path, out_dos, out_nt, out_nt_size, 0);
}

const char *get_pe_path(void) { return g_pe_path; }
void set_pe_path(const char *path)
{
    size_t i;
    for (i = 0; path[i] && i < sizeof(g_pe_path) - 1; i++)
        g_pe_path[i] = path[i];
    g_pe_path[i] = '\0';
}
