/*
 * image_mapper.c — PE image mapping
 *
 * Opens a PE file, maps it to the preferred base address, copies
 * section data, sets per-section memory protections, and cleans up.
 *
 * PE32 support: maps at 0x00400000 (default PE32 image base), sets
 * is_32bit flag in g_loader for use by other loader modules.
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
#include "loader_state.h"
#include "include/common.h"

#if defined(MY_WINE32)
/* Standalone 32-bit: use inline syscalls instead of libc */
#define wine_mmap(a, l, p, f, fd, o) INLINE_SYSCALL_MMAP(a, l, p, f, fd, o)
#define wine_munmap(a, l) INLINE_SYSCALL_MUNMAP(a, l)
#define wine_mprotect(a, l, p) INLINE_SYSCALL_MPROTECT(a, l, p)
#else
#define wine_mmap(a, l, p, f, fd, o) mmap(a, l, p, f, fd, o)
#define wine_munmap(a, l) munmap(a, l)
#define wine_mprotect(a, l, p) mprotect(a, l, p)
#endif

wine_loader_state_t g_loader = {0};

/**
 * Internal core: map a PE file at the given desired base address.
 *
 * When desired_base is non-zero, maps at that address instead of the
 * PE's preferred ImageBase.  For PE32 images with desired_base == 0,
 * uses PE32_DEFAULT_IMAGE_BASE (0x00400000).  For PE32+ with desired_base
 * == 0, uses the PE's preferred ImageBase.
 *
 * Opens the file, maps read-only, parses headers, maps the image
 * memory, copies sections, sets per-section protections, cleans up.
 *
 * @param  path         path to the PE file
 * @param  out_dos      (optional) receives parsed DOS header
 * @param  out_nt       (optional) receives parsed NT headers
 * @param  out_nt_size  (optional) receives size of parsed NT headers struct
 * @param  desired_base forced image base (0 = use PE's preferred or PE32 default)
 * @return  image base address (virtual), or NULL on failure
 */
void *map_image_at(const char *path,
                   IMAGE_DOS_HEADER *out_dos,
                   IMAGE_NT_HEADERS *out_nt,
                   size_t *out_nt_size,
                   uintptr_t desired_base)
{
    /* Save PE path for DLL search — hand-rolled copy, no glibc */
    {
        size_t i;
        for (i = 0; path[i] && i < sizeof(g_loader.pe_path) - 1; i++)
            g_loader.pe_path[i] = path[i];
        g_loader.pe_path[i] = '\0';
    }

    /* 1. Open the PE file */
    long fd = INLINE_SYSCALL_OPENAT(AT_FDCWD, path, O_RDONLY, 0);
    if (fd < 0) { DEBUG("open failed"); return NULL; }

    struct stat st;
    if (INLINE_SYSCALL_FSTAT(fd, &st) < 0) { DEBUG("fstat failed"); INLINE_SYSCALL_CLOSE(fd); return NULL; }
    size_t file_size = (size_t)st.st_size;

    /* 2. Map file read-only */
    void *file_base = wine_mmap(NULL, file_size, PROT_READ, MAP_PRIVATE, fd, 0);
    if (file_base == MAP_FAILED) { DEBUG("mmap file failed"); INLINE_SYSCALL_CLOSE(fd); return NULL; }

    /* 3. Parse headers */
    IMAGE_DOS_HEADER dos;
    if (parse_dos_header(file_base, file_size, &dos) != 0) {
        DEBUG("Invalid DOS header");
        wine_munmap(file_base, file_size);
        INLINE_SYSCALL_CLOSE(fd);
        return NULL;
    }

    IMAGE_NT_HEADERS nt;
    int parse_result = parse_nt_headers(file_base, file_size, &dos, &nt);
    if (parse_result != 0) {
        DEBUG("Invalid NT headers");
        wine_munmap(file_base, file_size);
        INLINE_SYSCALL_CLOSE(fd);
        return NULL;
    }

    IMAGE_SECTION_HEADER *sections = NULL;
    int num_sections = parse_sections(file_base, file_size, &nt, &sections);
    if (num_sections < 0) {
        DEBUG("Failed to parse sections");
        wine_munmap(file_base, file_size);
        INLINE_SYSCALL_CLOSE(fd);
        return NULL;
    }

    dump_headers(&dos, &nt, sections);

#if !defined(MY_WINE32)
    /* Force flush before debug output (stdout not initialized in standalone 32-bit) */
    fflush(stdout);
#endif

    /* Return parsed headers to caller (if requested) */
    if (out_dos) {
        *out_dos = dos;
    }
    if (out_nt) {
        *out_nt = nt;
        if (out_nt_size) {
            *out_nt_size = sizeof(uint32_t) + sizeof(IMAGE_FILE_HEADER) +
                           pe_optional_header_size(&nt);
        }
    }

    /* Detect 32-bit vs 64-bit image and set global flag.
     * g_loader.is_32bit is derived from nt.pe_type for use by other loader modules
     * that don't have direct access to the NT headers struct.
     * Transitional: also set g_is_32bit until task-5 migrates cross-module refs. */
    g_loader.is_32bit = g_is_32bit = pe_is_pe32(&nt) ? 1 : 0;

    /* 4. Map image */
    size_t image_size = pe_size_of_image(&nt);
    uint32_t section_alignment = pe_section_alignment(&nt);

    (void)section_alignment; /* Used by caller; available via accessor */

    /* Determine the image base address */
    uint64_t image_base;
    if (desired_base != 0) {
        image_base = (uint64_t)desired_base;

        /* For PE32, ensure the forced base fits in 32-bit address space */
        if (pe_is_pe32(&nt) && image_base >= ADDR32_LIMIT) {
            DEBUG("PE32: forced base 0x%lx exceeds 32-bit address space, "
                  "falling back to default 0x%lx",
                  (unsigned long)image_base, (unsigned long)PE32_DEFAULT_IMAGE_BASE);
            image_base = PE32_DEFAULT_IMAGE_BASE;
        }
    } else if (pe_is_pe32(&nt)) {
        /* PE32: use default 0x00400000 (always fits in 32-bit space) */
        image_base = PE32_DEFAULT_IMAGE_BASE;
    } else {
        /* PE32+: use the PE's preferred ImageBase via accessor */
        image_base = pe_image_base(&nt);
    }

    /* For PE32, verify the final address fits in 32-bit address space */
    if (pe_is_pe32(&nt)) {
        if (image_base >= ADDR32_LIMIT) {
            DEBUG("PE32: computed base 0x%lx exceeds 32-bit address space, "
                  "forcing 0x%lx",
                  (unsigned long)image_base, (unsigned long)PE32_DEFAULT_IMAGE_BASE);
            image_base = PE32_DEFAULT_IMAGE_BASE;
        }
        if ((image_base + image_size) >= ADDR32_LIMIT) {
            DEBUG("PE32: image extends beyond 32-bit address space, "
                  "forcing base to 0x%lx",
                  (unsigned long)PE32_DEFAULT_IMAGE_BASE);
            image_base = PE32_DEFAULT_IMAGE_BASE;
        }
    }

    void *base = wine_mmap((void *)(uintptr_t)image_base, image_size,
                       PROT_READ|PROT_WRITE|PROT_EXEC,
                       MAP_PRIVATE|MAP_ANONYMOUS|MAP_FIXED, -1, 0);
    if (base == MAP_FAILED) {
        DEBUG("MAP_FIXED at 0x%lx failed for %s image, trying without MAP_FIXED",
              (unsigned long)image_base,
              pe_is_pe32(&nt) ? "PE32" : "PE32+");

        base = wine_mmap(NULL, image_size,
                     PROT_READ|PROT_WRITE|PROT_EXEC,
                     MAP_PRIVATE|MAP_ANONYMOUS|MAP_STACK, -1, 0);
        if (base == MAP_FAILED) {
            DEBUG("mmap image failed");
            wine_munmap(file_base, file_size);
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
        uint32_t headers_size = pe_size_of_headers(&nt);
        if (headers_size > 0) {
            memcpy(base, file_base, headers_size);
            /* Re-point sections into the image */
            sections = get_image_sections(base, &nt);
        }
    }

    /* Apply base relocations (needed when actual base != preferred ImageBase) */
    if (apply_relocations(base, &nt) != 0) {
        DEBUG("Failed to apply relocations");
        wine_munmap(base, image_size);
        wine_munmap(file_base, file_size);
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

        if (wine_mprotect((char *)base + sections[i].VirtualAddress, size, prot) != 0) {
            DEBUG("mprotect failed");
            wine_munmap(base, image_size);
            wine_munmap(file_base, file_size);
            INLINE_SYSCALL_CLOSE(fd);
            return NULL;
        }
    }

    /* Unmap the original file mapping (no longer needed) */
    wine_munmap(file_base, file_size);
    INLINE_SYSCALL_CLOSE(fd);

    /* Save the image base for later use (import resolution, TEB/PEB, etc.) */
    g_loader.image_base = base;
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
                IMAGE_NT_HEADERS *out_nt,
                size_t *out_nt_size)
{
    return map_image_at(path, out_dos, out_nt, out_nt_size, 0);
}

const char *get_pe_path(void) { return g_loader.pe_path; }
void set_pe_path(const char *path)
{
    size_t i;
    for (i = 0; path[i] && i < sizeof(g_loader.pe_path) - 1; i++)
        g_loader.pe_path[i] = path[i];
    g_loader.pe_path[i] = '\0';
}
