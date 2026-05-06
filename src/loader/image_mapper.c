/*
 * image_mapper.c — PE image mapping
 *
 * Opens a PE file, maps it to the preferred base address, copies
 * section data, sets per-section memory protections, and cleans up.
 */

#define _GNU_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>

#include "include/pe.h"
#include "include/nt_constants.h"
#include "include/debug.h"
#include "loader_priv.h"

void *g_image_base = NULL;
static char g_pe_path[512] = {0};

/**
 * Map a PE file at the preferred image base.
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
    snprintf(g_pe_path, sizeof(g_pe_path), "%s", path);

    /* 1. Open the PE file */
    int fd = open(path, O_RDONLY);
    if (fd < 0) { perror("open"); return NULL; }

    struct stat st;
    if (fstat(fd, &st) < 0) { perror("fstat"); close(fd); return NULL; }
    size_t file_size = (size_t)st.st_size;

    /* 2. Map file read-only */
    void *file_base = mmap(NULL, file_size, PROT_READ, MAP_PRIVATE, fd, 0);
    if (file_base == MAP_FAILED) { perror("mmap file"); close(fd); return NULL; }

    /* 3. Parse headers */
    IMAGE_DOS_HEADER dos;
    if (parse_dos_header(file_base, file_size, &dos) != 0) {
        DEBUG("Invalid DOS header");
        munmap(file_base, file_size);
        close(fd);
        return NULL;
    }

    IMAGE_NT_HEADERS64 nt;
    if (parse_nt_headers(file_base, file_size, &dos, &nt) != 0) {
        DEBUG("Invalid NT headers");
        munmap(file_base, file_size);
        close(fd);
        return NULL;
    }

    IMAGE_SECTION_HEADER *sections = NULL;
    int num_sections = parse_sections(file_base, file_size, &nt, &sections);
    if (num_sections < 0) {
        DEBUG("Failed to parse sections");
        munmap(file_base, file_size);
        close(fd);
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
    uint64_t image_base = nt.OptionalHeader.ImageBase;
    size_t image_size   = nt.OptionalHeader.SizeOfImage;

    void *base = mmap((void *)(uintptr_t)image_base, image_size,
                       PROT_READ|PROT_WRITE|PROT_EXEC,
                       MAP_PRIVATE|MAP_ANONYMOUS|MAP_FIXED, -1, 0);
    if (base == MAP_FAILED) {

        base = mmap(NULL, image_size,
                     PROT_READ|PROT_WRITE|PROT_EXEC,
                     MAP_PRIVATE|MAP_ANONYMOUS|MAP_STACK, -1, 0);
        if (base == MAP_FAILED) {
            perror("mmap image");
            munmap(file_base, file_size);
            close(fd);
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
            const IMAGE_DOS_HEADER *img_dos = (const IMAGE_DOS_HEADER *)base;
            uint32_t pe_off = img_dos->e_lfanew;
            size_t sec_off  = pe_off + sizeof(uint32_t) + sizeof(IMAGE_FILE_HEADER) +
                              nt.FileHeader.SizeOfOptionalHeader;
            sections = (IMAGE_SECTION_HEADER *)((char *)base + sec_off);
        }
    }

    /* Apply base relocations (needed when actual base != preferred ImageBase) */
    if (apply_relocations(base, &nt) != 0) {
        DEBUG("Failed to apply relocations");
        munmap(base, image_size);
        munmap(file_base, file_size);
        close(fd);
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
        size = (size + 4095) & ~(size_t)4095;

        if (mprotect((char *)base + sections[i].VirtualAddress, size, prot) != 0) {
            perror("mprotect");
            munmap(base, image_size);
            munmap(file_base, file_size);
            close(fd);
            return NULL;
        }
    }

    /* Unmap the original file mapping (no longer needed) */
    munmap(file_base, file_size);
    close(fd);

    /* Save the image base for later use (import resolution, TEB/PEB, etc.) */
    g_image_base = base;
    return base;
}

const char *get_pe_path(void) { return g_pe_path; }
