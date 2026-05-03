/*
 * test_parse.c — standalone PE parser test
 *
 * Opens a PE binary, maps it into memory, runs the parser from pe.c,
 * and verifies the PE32+ structure of hello.exe.
 *
 * Usage: ./test_parse hello.exe  (or path as argv[1])
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#include "pe.h"

/* Forward declarations from pe.c */
int parse_dos_header(const void *base, size_t file_size, IMAGE_DOS_HEADER *out_header);
int parse_nt_headers(const void *base, size_t file_size,
                     const IMAGE_DOS_HEADER *dos_header,
                     IMAGE_NT_HEADERS64 *out_nt_headers);
int parse_sections(const void *base, size_t file_size,
                   const IMAGE_NT_HEADERS64 *nt_headers,
                   IMAGE_SECTION_HEADER **out_sections);
int parse_imports(const void *base, size_t file_size,
                  const IMAGE_NT_HEADERS64 *nt_headers,
                  IMAGE_IMPORT_DESCRIPTOR **out_first_descriptor);
void dump_headers(const IMAGE_DOS_HEADER *dos, const IMAGE_NT_HEADERS64 *nt,
                  const IMAGE_SECTION_HEADER *sections);

static int failed = 0;

static void check(const char *label, int condition)
{
    if (condition)
        printf("PASS: %s\n", label);
    else {
        printf("FAIL: %s\n", label);
        failed = 1;
    }
}

int main(int argc, char *argv[])
{
    const char *path = "hello.exe";

    if (argc > 1)
        path = argv[1];

    /* ── Open file ──────────────────────────────────────────────── */
    int fd = open(path, O_RDONLY);
    if (fd < 0) {
        fprintf(stderr, "Error: cannot open '%s'\n", path);
        return 1;
    }

    /* ── Get file size ──────────────────────────────────────────── */
    struct stat st;
    if (fstat(fd, &st) < 0) {
        fprintf(stderr, "Error: cannot stat '%s'\n", path);
        close(fd);
        return 1;
    }

    size_t file_size = (size_t)st.st_size;
    if (file_size == 0) {
        fprintf(stderr, "Error: '%s' is empty\n", path);
        close(fd);
        return 1;
    }

    /* ── Map file into memory ───────────────────────────────────── */
    const void *base = mmap(NULL, file_size, PROT_READ, MAP_PRIVATE, fd, 0);
    if (base == MAP_FAILED) {
        fprintf(stderr, "Error: mmap failed for '%s'\n", path);
        close(fd);
        return 1;
    }

    /* ── 1. DOS Header ──────────────────────────────────────────── */
    IMAGE_DOS_HEADER dos_header;
    int rc = parse_dos_header(base, file_size, &dos_header);
    check("DOS header MZ magic present", rc == 0);
    if (rc != 0) {
        fprintf(stderr, "Error: parse_dos_header failed (rc=%d)\n", rc);
        munmap((void *)base, file_size);
        close(fd);
        return 1;
    }

    check("e_magic == IMAGE_DOS_SIGNATURE",
          dos_header.e_magic == IMAGE_DOS_SIGNATURE);

    /* ── 2. NT Headers ──────────────────────────────────────────── */
    IMAGE_NT_HEADERS64 nt_headers;
    rc = parse_nt_headers(base, file_size, &dos_header, &nt_headers);
    check("PE signature valid", rc == 0);
    if (rc != 0) {
        fprintf(stderr, "Error: parse_nt_headers failed (rc=%d)\n", rc);
        munmap((void *)base, file_size);
        close(fd);
        return 1;
    }

    check("Machine == IMAGE_FILE_MACHINE_AMD64 (0x8664)",
          nt_headers.FileHeader.Machine == IMAGE_FILE_MACHINE_AMD64);

    /* ── 3. Sections ────────────────────────────────────────────── */
    IMAGE_SECTION_HEADER *sections = NULL;
    int num_sections = parse_sections(base, file_size, &nt_headers, &sections);
    check("Section count > 0", num_sections > 0);

    /* ── 4. Imports ─────────────────────────────────────────────── */
    IMAGE_IMPORT_DESCRIPTOR *first_desc = NULL;
    int num_imports = parse_imports(base, file_size, &nt_headers, &first_desc);
    check("Import directory not null (at least one import)",
          num_imports > 0 && first_desc != NULL);

    /* ── 5. Dump headers ────────────────────────────────────────── */
    if (sections)
        dump_headers(&dos_header, &nt_headers, sections);

    /* ── Summary ────────────────────────────────────────────────── */
    printf("\nPE32+ detected, %d sections, %d imports\n",
           num_sections > 0 ? num_sections : 0,
           num_imports > 0 ? num_imports : 0);

    /* ── Cleanup ────────────────────────────────────────────────── */
    munmap((void *)base, file_size);
    close(fd);

    return failed ? 1 : 0;
}
