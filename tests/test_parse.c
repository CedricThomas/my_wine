/*
 * test_parse.c — standalone PE parser test
 *
 * Opens a PE binary, maps it into memory, runs the parser from pe_parser.c,
 * and verifies the PE32+ structure of a valid PE file (positive tests),
 * then runs a suite of error/negative tests with crafted temp files.
 *
 * Usage: ./test_parse [path_to_pe_file]
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#include <errno.h>

#include "pe.h"

/* Forward declarations from pe_parser.c */
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

/* ── Test harness ─────────────────────────────────────────────── */

static int total_tests = 0;
static int passed_tests = 0;
static int failed_tests = 0;

static void check(const char *label, int condition)
{
    total_tests++;
    if (condition) {
        printf("  PASS: %s\n", label);
        passed_tests++;
    } else {
        printf("  FAIL: %s\n", label);
        failed_tests++;
    }
}



/* ── Temp file helpers for negative tests ─────────────────────── */

/* Create a temp file with the given content, return fd (caller closes).
 * File is automatically unlinked. */
static int create_temp_file(const void *data, size_t len, const char *label)
{
    char path[] = "/tmp/test_parse_XXXXXX";
    int fd = mkstemp(path);
    if (fd < 0) {
        fprintf(stderr, "Cannot create temp file for '%s': %s\n", label, strerror(errno));
        return -1;
    }
    unlink(path); /* auto-remove on exit */
    if (len > 0) {
        ssize_t w = write(fd, data, len);
        if ((size_t)w != len) {
            fprintf(stderr, "Short write in temp file '%s': %s\n", label, strerror(errno));
            close(fd);
            return -1;
        }
    }
    return fd;
}

/* Map a temp fd, return (void*)base. Caller munmaps + closes fd. */
static void *map_temp_fd(int fd, size_t size)
{
    void *base = mmap(NULL, size, PROT_READ, MAP_PRIVATE, fd, 0);
    if (base == MAP_FAILED)
        return NULL;
    return base;
}

/* ── Positive tests (run against a real PE file) ──────────────── */

static void test_positive(const char *path)
{
    printf("\n=== Positive Tests (valid PE: %s) ===\n", path);

    int fd = open(path, O_RDONLY);
    if (fd < 0) {
        fprintf(stderr, "Error: cannot open '%s'\n", path);
        return;
    }

    struct stat st;
    if (fstat(fd, &st) < 0) {
        fprintf(stderr, "Error: cannot stat '%s'\n", path);
        close(fd);
        return;
    }

    size_t file_size = (size_t)st.st_size;
    if (file_size == 0) {
        fprintf(stderr, "Error: '%s' is empty\n", path);
        close(fd);
        return;
    }

    const void *base = mmap(NULL, file_size, PROT_READ, MAP_PRIVATE, fd, 0);
    if (base == MAP_FAILED) {
        fprintf(stderr, "Error: mmap failed for '%s'\n", path);
        close(fd);
        return;
    }

    /* 1. DOS Header */
    IMAGE_DOS_HEADER dos_header;
    int rc = parse_dos_header(base, file_size, &dos_header);
    check("DOS header parse succeeds", rc == 0);
    check("e_magic == IMAGE_DOS_SIGNATURE",
          dos_header.e_magic == IMAGE_DOS_SIGNATURE);

    if (rc != 0) {
        munmap((void *)base, file_size);
        close(fd);
        return;
    }

    /* 2. NT Headers */
    IMAGE_NT_HEADERS64 nt_headers;
    rc = parse_nt_headers(base, file_size, &dos_header, &nt_headers);
    check("NT headers parse succeeds", rc == 0);
    check("PE signature valid",
          nt_headers.Signature == IMAGE_NT_SIGNATURE);
    check("Machine == IMAGE_FILE_MACHINE_AMD64",
          nt_headers.FileHeader.Machine == IMAGE_FILE_MACHINE_AMD64);
    check("Optional header magic == PE32+",
          nt_headers.OptionalHeader.Magic == IMAGE_NT_OPTIONAL_HDR64_MAGIC);

    if (rc != 0) {
        munmap((void *)base, file_size);
        close(fd);
        return;
    }

    /* 3. Sections */
    IMAGE_SECTION_HEADER *sections = NULL;
    int num_sections = parse_sections(base, file_size, &nt_headers, &sections);
    check("Section count > 0", num_sections > 0);

    /* 4. Imports */
    IMAGE_IMPORT_DESCRIPTOR *first_desc = NULL;
    int num_imports = parse_imports(base, file_size, &nt_headers, &first_desc);
    check("Import directory not null (at least one import)",
          num_imports > 0 && first_desc != NULL);

    /* 5. Dump headers */
    if (sections)
        dump_headers(&dos_header, &nt_headers, sections);

    printf("\n  PE32+ detected, %d sections, %d imports\n",
           num_sections > 0 ? num_sections : 0,
           num_imports > 0 ? num_imports : 0);

    munmap((void *)base, file_size);
    close(fd);
}

/* ── t7.2: Error case tests ───────────────────────────────────── */

static void test_bad_mz_signature(void)
{
    printf("\n--- Bad MZ signature ---\n");

    /* Create a file that looks like nothing (just zeros) */
    unsigned char buf[256] = {0};
    int fd = create_temp_file(buf, sizeof(buf), "bad_mz");
    if (fd < 0) return;

    void *base = map_temp_fd(fd, sizeof(buf));
    if (!base) { close(fd); return; }

    IMAGE_DOS_HEADER hdr;
    int rc = parse_dos_header(base, sizeof(buf), &hdr);
    check("parse_dos_header fails on bad MZ signature", rc == -1);

    munmap(base, sizeof(buf));
    close(fd);
}

static void test_bad_pe_signature(void)
{
    printf("\n--- Bad PE signature ---\n");

    /* Create a file with valid MZ but garbage at e_lfanew */
    unsigned char buf[1024] = {0};
    IMAGE_DOS_HEADER *dos = (IMAGE_DOS_HEADER *)buf;
    dos->e_magic = IMAGE_DOS_SIGNATURE;
    dos->e_lfanew = 64; /* PE sig at offset 64 */
    /* Write something that is NOT "PE\0\0" at offset 64 */
    *(uint32_t *)(buf + 64) = 0x41414141; /* "AAAA" */

    int fd = create_temp_file(buf, sizeof(buf), "bad_pe_sig");
    if (fd < 0) return;

    void *base = map_temp_fd(fd, sizeof(buf));
    if (!base) { close(fd); return; }

    IMAGE_DOS_HEADER dos_header;
    int rc = parse_dos_header(base, sizeof(buf), &dos_header);
    check("parse_dos_header succeeds (MZ is valid)", rc == 0);

    IMAGE_NT_HEADERS64 nt;
    rc = parse_nt_headers(base, sizeof(buf), &dos_header, &nt);
    check("parse_nt_headers fails on bad PE signature", rc == -1);

    munmap(base, sizeof(buf));
    close(fd);
}

static void test_non_amd64_machine(void)
{
    printf("\n--- Non-AMD64 machine type ---\n");

    /* Build a minimal PE32+ header with x86 (0x14c) machine type */
    unsigned char buf[1024] = {0};
    IMAGE_DOS_HEADER *dos = (IMAGE_DOS_HEADER *)buf;
    dos->e_magic = IMAGE_DOS_SIGNATURE;
    dos->e_lfanew = 64;

    IMAGE_NT_HEADERS64 *nt = (IMAGE_NT_HEADERS64 *)(buf + 64);
    nt->Signature = IMAGE_NT_SIGNATURE;
    nt->FileHeader.Machine = 0x14c; /* IMAGE_FILE_MACHINE_I386 */
    nt->FileHeader.NumberOfSections = 0;
    nt->FileHeader.SizeOfOptionalHeader = sizeof(IMAGE_OPTIONAL_HEADER64);
    nt->OptionalHeader.Magic = IMAGE_NT_OPTIONAL_HDR64_MAGIC;

    int fd = create_temp_file(buf, sizeof(buf), "non_amd64");
    if (fd < 0) return;

    void *base = map_temp_fd(fd, sizeof(buf));
    if (!base) { close(fd); return; }

    IMAGE_DOS_HEADER dos_header;
    int rc = parse_dos_header(base, sizeof(buf), &dos_header);
    check("parse_dos_header succeeds", rc == 0);

    IMAGE_NT_HEADERS64 nt_out;
    rc = parse_nt_headers(base, sizeof(buf), &dos_header, &nt_out);
    check("parse_nt_headers fails on non-AMD64 machine", rc == -1);

    munmap(base, sizeof(buf));
    close(fd);
}

static void test_truncated_file(void)
{
    printf("\n--- Truncated file ---\n");

    /* Just 4 bytes — less than a DOS header */
    unsigned char buf[4] = {'M', 'Z', 0, 0};
    int fd = create_temp_file(buf, sizeof(buf), "truncated");
    if (fd < 0) return;

    void *base = map_temp_fd(fd, sizeof(buf));
    if (!base) { close(fd); return; }

    IMAGE_DOS_HEADER hdr;
    int rc = parse_dos_header(base, sizeof(buf), &hdr);
    check("parse_dos_header fails on truncated file", rc == -1);

    munmap(base, sizeof(buf));
    close(fd);
}

static void test_empty_imports_directory(void)
{
    printf("\n--- Empty imports directory ---\n");

    /* Build a minimal valid PE with no import directory (VA=0, Size=0) */
    unsigned char buf[2048] = {0};
    IMAGE_DOS_HEADER *dos = (IMAGE_DOS_HEADER *)buf;
    dos->e_magic = IMAGE_DOS_SIGNATURE;
    dos->e_lfanew = 64;

    IMAGE_NT_HEADERS64 *nt = (IMAGE_NT_HEADERS64 *)(buf + 64);
    nt->Signature = IMAGE_NT_SIGNATURE;
    nt->FileHeader.Machine = IMAGE_FILE_MACHINE_AMD64;
    nt->FileHeader.NumberOfSections = 0;
    nt->FileHeader.SizeOfOptionalHeader = sizeof(IMAGE_OPTIONAL_HEADER64);
    nt->OptionalHeader.Magic = IMAGE_NT_OPTIONAL_HDR64_MAGIC;
    nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT].VirtualAddress = 0;
    nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT].Size = 0;

    int fd = create_temp_file(buf, sizeof(buf), "empty_imports");
    if (fd < 0) return;

    void *base = map_temp_fd(fd, sizeof(buf));
    if (!base) { close(fd); return; }

    IMAGE_DOS_HEADER dos_header;
    int rc = parse_dos_header(base, sizeof(buf), &dos_header);
    check("parse_dos_header succeeds", rc == 0);

    IMAGE_NT_HEADERS64 nt_out;
    rc = parse_nt_headers(base, sizeof(buf), &dos_header, &nt_out);
    check("parse_nt_headers succeeds", rc == 0);

    IMAGE_IMPORT_DESCRIPTOR *first = NULL;
    int num = parse_imports(base, sizeof(buf), &nt_out, &first);
    check("parse_imports returns 0 for empty import dir", num == 0);
    check("parse_imports sets output to NULL for empty imports", first == NULL);

    munmap(base, sizeof(buf));
    close(fd);
}

static void test_overlapping_sections(void)
{
    printf("\n--- Overlapping sections ---\n");

    /* Build a minimal PE with two overlapping sections
     * (same PointerToRawData range) */
    unsigned char buf[4096] = {0};
    IMAGE_DOS_HEADER *dos = (IMAGE_DOS_HEADER *)buf;
    dos->e_magic = IMAGE_DOS_SIGNATURE;
    dos->e_lfanew = 64;

    IMAGE_NT_HEADERS64 *nt = (IMAGE_NT_HEADERS64 *)(buf + 64);
    nt->Signature = IMAGE_NT_SIGNATURE;
    nt->FileHeader.Machine = IMAGE_FILE_MACHINE_AMD64;
    nt->FileHeader.NumberOfSections = 2;
    nt->FileHeader.SizeOfOptionalHeader = sizeof(IMAGE_OPTIONAL_HEADER64);
    nt->OptionalHeader.Magic = IMAGE_NT_OPTIONAL_HDR64_MAGIC;

    /* Section table follows NT headers */
    size_t sec_off = 64 + sizeof(uint32_t) + sizeof(IMAGE_FILE_HEADER) + sizeof(IMAGE_OPTIONAL_HEADER64);
    IMAGE_SECTION_HEADER *sec = (IMAGE_SECTION_HEADER *)(buf + sec_off);

    /* Section 1: raw data at 512, size 512 */
    memcpy(sec[0].Name, ".text\0\0\0", 8);
    sec[0].Misc.VirtualSize = 0x200;
    sec[0].VirtualAddress = 0x1000;
    sec[0].SizeOfRawData = 0x200;
    sec[0].PointerToRawData = 512;
    sec[0].Characteristics = IMAGE_SCN_MEM_READ | IMAGE_SCN_MEM_EXECUTE;

    /* Section 2: overlapping raw data (starts at 700 within section 1's range 512-1024) */
    memcpy(sec[1].Name, ".rdata\0\0", 8);
    sec[1].Misc.VirtualSize = 0x100;
    sec[1].VirtualAddress = 0x2000;
    sec[1].SizeOfRawData = 0x100;
    sec[1].PointerToRawData = 700; /* overlaps with .text raw range [512, 1024) */
    sec[1].Characteristics = IMAGE_SCN_MEM_READ;

    int fd = create_temp_file(buf, sizeof(buf), "overlapping_sections");
    if (fd < 0) return;

    void *base = map_temp_fd(fd, sizeof(buf));
    if (!base) { close(fd); return; }

    IMAGE_DOS_HEADER dos_header;
    int rc = parse_dos_header(base, sizeof(buf), &dos_header);
    check("parse_dos_header succeeds", rc == 0);

    IMAGE_NT_HEADERS64 nt_out;
    rc = parse_nt_headers(base, sizeof(buf), &dos_header, &nt_out);
    check("parse_nt_headers succeeds", rc == 0);

    IMAGE_SECTION_HEADER *sections = NULL;
    int num = parse_sections(base, sizeof(buf), &nt_out, &sections);
    /* parse_sections validates raw data bounds but does not currently
     * reject overlapping sections — it returns the count without error */
    check("parse_sections returns 2 sections (overlaps not rejected)", num == 2);
    check("sections[0] is .text", strncmp((char *)sections[0].Name, ".text", 5) == 0);
    check("sections[1] is .rdata", strncmp((char *)sections[1].Name, ".rdata", 6) == 0);

    munmap(base, sizeof(buf));
    close(fd);
}

/* ── t7.6: Negative tests ─────────────────────────────────────── */

static void test_file_not_found(void)
{
    printf("\n--- File not found ---\n");

    int fd = open("/tmp/nonexistent_test_file_xyz123.exe", O_RDONLY);
    check("open() fails for nonexistent file", fd < 0);
    if (fd >= 0)
        close(fd);
}

static void test_empty_file(void)
{
    printf("\n--- Empty file ---\n");

    int fd = create_temp_file(NULL, 0, "empty");
    if (fd < 0) return;

    /* fstat to get size */
    struct stat st;
    fstat(fd, &st);

    if (st.st_size == 0) {
        check("empty file has size 0", st.st_size == 0);
        /* mmap with size 0 is implementation-defined; parse_dos_header checks size < sizeof */
        /* We test via a direct call with size=0 */
        IMAGE_DOS_HEADER hdr;
        int rc = parse_dos_header(NULL, 0, &hdr);
        check("parse_dos_header fails on 0-size input", rc == -1);
    }

    close(fd);
}

static void test_text_file(void)
{
    printf("\n--- Text file (not a PE) ---\n");

    const char *text = "This is just a plain text file.\nNot a PE binary at all.\n";
    int fd = create_temp_file(text, strlen(text), "text_file");
    if (fd < 0) return;

    void *base = map_temp_fd(fd, strlen(text));
    if (!base) { close(fd); return; }

    IMAGE_DOS_HEADER hdr;
    int rc = parse_dos_header(base, strlen(text), &hdr);
    check("parse_dos_header fails on text file", rc == -1);

    munmap(base, strlen(text));
    close(fd);
}

static void test_mmap_failure(void)
{
    printf("\n--- mmap failure (huge allocation) ---\n");

    /* Attempt to mmap a ridiculously large size — should fail */
    size_t huge_size = (size_t)1 << 40; /* 1 TiB */
    void *base = mmap(NULL, huge_size, PROT_READ, MAP_PRIVATE, -1, 0);
    check("mmap fails for huge allocation", base == MAP_FAILED);

    /* Also test with a real small file but requesting more than file size */
    int fd = create_temp_file("small", 5, "mmap_fail_test");
    if (fd >= 0) {
        /* mmap the file with a size larger than the file — this is actually
         * allowed (reads as zeros beyond EOF), but a size of 0 should fail
         * or return MAP_FAILED depending on implementation.
         * The real test: a legitimately large request. */
        close(fd);
    }
}

/* ── Main ──────────────────────────────────────────────────────── */

int main(int argc, char *argv[])
{
    const char *pe_path = NULL;

    /* Try to find a PE file for positive tests */
    if (argc > 1) {
        pe_path = argv[1];
    } else {
        /* Check common locations */
        if (access("hello.exe", F_OK) == 0) {
            pe_path = "hello.exe";
        } else if (access("examples/hello.exe", F_OK) == 0) {
            pe_path = "examples/hello.exe";
        }
    }

    /* ── Positive tests (if we have a PE file) ─────────────────── */
    if (pe_path) {
        test_positive(pe_path);
    } else {
        printf("\n=== Positive Tests: SKIPPED (no PE file found) ===\n");
    }

    /* ── t7.2: Error case tests ─────────────────────────────────── */
    printf("\n=== Error Case Tests ===\n");
    test_bad_mz_signature();
    test_bad_pe_signature();
    test_non_amd64_machine();
    test_truncated_file();
    test_empty_imports_directory();
    test_overlapping_sections();

    /* ── t7.6: Negative tests ───────────────────────────────────── */
    printf("\n=== Negative Tests ===\n");
    test_file_not_found();
    test_empty_file();
    test_text_file();
    test_mmap_failure();

    /* ── Summary ─────────────────────────────────────────────────── */
    printf("\n========================================\n");
    printf("Total:  %d  Passed: %d  Failed: %d\n",
           total_tests, passed_tests, failed_tests);
    printf("========================================\n");

    return failed_tests > 0 ? 1 : 0;
}
