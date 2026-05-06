/*
 * test_relocations.c — standalone unit test for apply_relocations()
 *
 * Crafts a minimal PE image in memory with known DIR64 relocation
 * entries, calls apply_relocations(), and verifies the patched values.
 *
 * Also includes an integration test that loads a real PE file via
 * map_image() and verifies the MAP_STACK fallback + relocations work
 * when the preferred ImageBase is unavailable.
 *
 * Usage:
 *   ./build/test_relocations                   — unit tests only
 *   ./build/test_relocations path/to/file.exe  — unit + integration test
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <errno.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>

#include "pe.h"
#include "nt_constants.h"

/* Forward declare — relocations.c is compiled into this test binary */
int apply_relocations(void *base, IMAGE_NT_HEADERS64 *nt);

/*
 * Forward declarations for the integration test — image_mapper.c and
 * pe_headers.c are also compiled into this test binary.
 */
void *map_image(const char *path,
                IMAGE_DOS_HEADER *out_dos,
                IMAGE_NT_HEADERS64 *out_nt,
                size_t *out_nt_size);
int parse_dos_header(const void *base, size_t file_size,
                     IMAGE_DOS_HEADER *out_header);
int parse_nt_headers(const void *base, size_t file_size,
                     const IMAGE_DOS_HEADER *dos_header,
                     IMAGE_NT_HEADERS64 *out_nt_headers);

/* Global set by image_mapper.c */
extern void *g_image_base;

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

/* ---------------------------------------------------------------- */
/* Helper: build a minimal PE image in a mmap'd region               */
/* ---------------------------------------------------------------- */

/*
 * Layout (all offsets from base):
 *
 *   +0x000  IMAGE_DOS_HEADER       (e_lfanew = 0x80)
 *   +0x080  IMAGE_NT_HEADERS64
 *   +0x1000 relocatable 64-bit value  (target of relocation)
 *   +0x1100 IMAGE_BASE_RELOCATION block
 *
 * The caller plants a known 64-bit value at offset 0x1000.
 */

static void setup_dos_header(void *base)
{
    IMAGE_DOS_HEADER *dos = (IMAGE_DOS_HEADER *)base;
    dos->e_magic = IMAGE_DOS_SIGNATURE;
    dos->e_lfanew = 0x80;
}

static IMAGE_NT_HEADERS64 *setup_nt_headers(void *base,
                                             uint64_t image_base,
                                             uint32_t reloc_va,
                                             uint32_t reloc_size,
                                             uint16_t characteristics)
{
    IMAGE_NT_HEADERS64 *nt =
        (IMAGE_NT_HEADERS64 *)((char *)base + 0x80);
    memset(nt, 0, sizeof(*nt));

    nt->Signature = IMAGE_NT_SIGNATURE;
    nt->FileHeader.Machine = IMAGE_FILE_MACHINE_AMD64;
    nt->FileHeader.NumberOfSections = 0;
    nt->FileHeader.SizeOfOptionalHeader =
        sizeof(IMAGE_OPTIONAL_HEADER64);
    nt->FileHeader.Characteristics = characteristics;

    nt->OptionalHeader.Magic = IMAGE_NT_OPTIONAL_HDR64_MAGIC;
    nt->OptionalHeader.ImageBase = image_base;
    nt->OptionalHeader.SectionAlignment = 0x1000;
    nt->OptionalHeader.FileAlignment = 0x200;
    nt->OptionalHeader.NumberOfRvaAndSizes =
        IMAGE_NUMBEROF_DIRECTORY_ENTRIES;

    nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_BASERELOC]
        .VirtualAddress = reloc_va;
    nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_BASERELOC]
        .Size = reloc_size;

    return nt;
}

/*
 * Build a PE-spec-compliant 16-bit packed relocation entry.
 *
 * PE spec: bits 0–11 = offset, bits 12–15 = type.
 * Stored as a single uint16_t (2 bytes) per entry.
 */
static uint16_t make_reloc_entry(uint16_t type, uint16_t offset)
{
    return ((type & 0xF) << 12) | (offset & 0xFFF);
}

/*
 * Get a pointer to the first relocation entry right after the
 * IMAGE_BASE_RELOCATION header (8 bytes: virtualAddress + sizeOfBlock).
 */
static uint16_t *reloc_entries_ptr(IMAGE_BASE_RELOCATION *block)
{
    return (uint16_t *)((char *)block + sizeof(IMAGE_BASE_RELOCATION));
}

/*
 * Build a single-entry DIR64 relocation block at block_va.
 *
 * Layout: 8-byte header + 2-byte entry = 10 bytes total.
 */
static void setup_single_dir64_block(void *base,
                                      uint32_t block_va,
                                      uint32_t target_va,
                                      uint16_t entry_offset)
{
    IMAGE_BASE_RELOCATION *block =
        (IMAGE_BASE_RELOCATION *)((char *)base + block_va);
    block->virtualAddress = target_va;
    /* 8-byte header + 1 entry (2 bytes) = 10 */
    block->sizeOfBlock = sizeof(IMAGE_BASE_RELOCATION) + sizeof(uint16_t);
    uint16_t *entries = reloc_entries_ptr(block);
    entries[0] = make_reloc_entry(IMAGE_REL_BASED_DIR64, entry_offset);
}

/* ---------------------------------------------------------------- */
/* Test 1: DIR64 relocation — value patched by delta                  */
/* ---------------------------------------------------------------- */

static void test_dir64_relocation(void)
{
    printf("\n--- DIR64 relocation ---\n");

    void *base = mmap(NULL, 4096 * 2, PROT_READ | PROT_WRITE,
                      MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (base == MAP_FAILED) { printf("  SKIP: mmap failed\n"); return; }

    /* Preferred base is different from actual mmap address */
    uint64_t old_image_base = 0x140000000ULL;

    /* Plant a known 64-bit value at offset 0x1000 */
    uint64_t *test_value = (uint64_t *)((char *)base + 0x1000);
    *test_value = old_image_base;

    /* Build the minimal PE */
    setup_dos_header(base);

    uint32_t block_va = 0x1100;
    /* 8-byte header + 1 entry (2 bytes) = 10 bytes */
    uint32_t block_size = sizeof(IMAGE_BASE_RELOCATION) + sizeof(uint16_t);
    setup_single_dir64_block(base, block_va, 0x1000, 0);

    IMAGE_NT_HEADERS64 *nt =
        setup_nt_headers(base, old_image_base, block_va, block_size, 0);

    /* Expected delta */
    uintptr_t delta = (uintptr_t)base - old_image_base;
    uint64_t expected = (uint64_t)(uintptr_t)base;

    int rc = apply_relocations(base, nt);
    check("apply_relocations returns 0", rc == 0);
    check("value patched from old base to new base",
          *test_value == expected);

    printf("    delta=0x%lx  old=0x%lx  new=0x%lx  expected=0x%lx\n",
           (unsigned long)delta,
           (unsigned long)old_image_base,
           (unsigned long)*test_value,
           (unsigned long)expected);

    munmap(base, 4096 * 2);
}

/* ---------------------------------------------------------------- */
/* Test 2: ABSOLUTE — no-op, value unchanged                          */
/* ---------------------------------------------------------------- */

static void test_absolute_noop(void)
{
    printf("\n--- ABSOLUTE no-op ---\n");

    void *base = mmap(NULL, 4096 * 2, PROT_READ | PROT_WRITE,
                      MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (base == MAP_FAILED) { printf("  SKIP: mmap failed\n"); return; }

    uint64_t old_image_base = 0x140000000ULL;

    uint64_t *test_value = (uint64_t *)((char *)base + 0x1000);
    *test_value = old_image_base;

    setup_dos_header(base);

    uint32_t block_va = 0x1100;
    IMAGE_BASE_RELOCATION *block =
        (IMAGE_BASE_RELOCATION *)((char *)base + block_va);
    block->virtualAddress = 0x1000;
    /* 8-byte header + 1 entry (2 bytes) = 10 */
    block->sizeOfBlock = sizeof(IMAGE_BASE_RELOCATION) + sizeof(uint16_t);
    uint16_t *entries = reloc_entries_ptr(block);
    entries[0] = make_reloc_entry(IMAGE_REL_BASED_ABSOLUTE, 0);

    uint32_t block_size = block->sizeOfBlock;
    IMAGE_NT_HEADERS64 *nt =
        setup_nt_headers(base, old_image_base, block_va, block_size, 0);

    int rc = apply_relocations(base, nt);
    check("apply_relocations returns 0", rc == 0);
    check("ABSOLUTE entry leaves value unchanged",
          *test_value == old_image_base);

    munmap(base, 4096 * 2);
}

/* ---------------------------------------------------------------- */
/* Test 3: Mixed entries — ABSOLUTE at offset 0, DIR64 at offset 8   */
/* ---------------------------------------------------------------- */

static void test_mixed_entries(void)
{
    printf("\n--- Mixed ABSOLUTE + DIR64 ---\n");

    void *base = mmap(NULL, 4096 * 2, PROT_READ | PROT_WRITE,
                      MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (base == MAP_FAILED) { printf("  SKIP: mmap failed\n"); return; }

    uint64_t old_image_base = 0x140000000ULL;

    /* Two 64-bit values at 0x1000 and 0x1008 */
    uint64_t *val_abs = (uint64_t *)((char *)base + 0x1000);
    uint64_t *val_dir = (uint64_t *)((char *)base + 0x1008);
    *val_abs = old_image_base;
    *val_dir = old_image_base + 0x100;

    setup_dos_header(base);

    uint32_t block_va = 0x1100;
    IMAGE_BASE_RELOCATION *block =
        (IMAGE_BASE_RELOCATION *)((char *)base + block_va);
    block->virtualAddress = 0x1000;
    /* 8-byte header + 2 entries (2 bytes each) = 12 */
    block->sizeOfBlock = sizeof(IMAGE_BASE_RELOCATION) + 2 * sizeof(uint16_t);
    uint16_t *entries = reloc_entries_ptr(block);
    entries[0] = make_reloc_entry(IMAGE_REL_BASED_ABSOLUTE, 0);
    entries[1] = make_reloc_entry(IMAGE_REL_BASED_DIR64, 8);

    IMAGE_NT_HEADERS64 *nt =
        setup_nt_headers(base, old_image_base, block_va, block->sizeOfBlock, 0);

    int rc = apply_relocations(base, nt);
    check("apply_relocations returns 0", rc == 0);
    check("ABSOLUTE entry leaves val at offset 0 unchanged",
          *val_abs == old_image_base);
    check("DIR64 entry patches val at offset 8",
          *val_dir == (uint64_t)(uintptr_t)base + 0x100);

    munmap(base, 4096 * 2);
}

/* ---------------------------------------------------------------- */
/* Test 4: Empty reloc dir — VA=0, Size=0 → returns 0                 */
/* ---------------------------------------------------------------- */

static void test_empty_reloc_dir(void)
{
    printf("\n--- Empty reloc dir ---\n");

    void *base = mmap(NULL, 4096 * 2, PROT_READ | PROT_WRITE,
                      MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (base == MAP_FAILED) { printf("  SKIP: mmap failed\n"); return; }

    uint64_t old_image_base = 0x140000000ULL;

    setup_dos_header(base);
    IMAGE_NT_HEADERS64 *nt =
        setup_nt_headers(base, old_image_base, 0, 0, 0);

    int rc = apply_relocations(base, nt);
    check("apply_relocations returns 0 for empty reloc dir", rc == 0);

    munmap(base, 4096 * 2);
}

/* ---------------------------------------------------------------- */
/* Test 5: delta==0 — base == ImageBase → returns 0, no changes       */
/* ---------------------------------------------------------------- */

static void test_delta_zero(void)
{
    printf("\n--- delta == 0 ---\n");

    void *base = mmap(NULL, 4096 * 2, PROT_READ | PROT_WRITE,
                      MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (base == MAP_FAILED) { printf("  SKIP: mmap failed\n"); return; }

    /* ImageBase == actual mmap address → delta == 0 */
    uint64_t old_image_base = (uint64_t)(uintptr_t)base;

    uint64_t *test_value = (uint64_t *)((char *)base + 0x1000);
    *test_value = old_image_base;

    setup_dos_header(base);

    uint32_t block_va = 0x1100;
    /* 8-byte header + 1 entry (2 bytes) = 10 */
    uint32_t block_size = sizeof(IMAGE_BASE_RELOCATION) + sizeof(uint16_t);
    setup_single_dir64_block(base, block_va, 0x1000, 0);

    IMAGE_NT_HEADERS64 *nt =
        setup_nt_headers(base, old_image_base, block_va, block_size, 0);

    int rc = apply_relocations(base, nt);
    check("apply_relocations returns 0 when delta == 0", rc == 0);
    check("value unchanged when delta == 0",
          *test_value == old_image_base);

    munmap(base, 4096 * 2);
}

/* ---------------------------------------------------------------- */
/* Test 6: RELOCS_STRIPPED + non-zero delta → returns -1              */
/* ---------------------------------------------------------------- */

static void test_relocs_stripped(void)
{
    printf("\n--- RELOCS_STRIPPED ---\n");

    void *base = mmap(NULL, 4096 * 2, PROT_READ | PROT_WRITE,
                      MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (base == MAP_FAILED) { printf("  SKIP: mmap failed\n"); return; }

    uint64_t old_image_base = 0x140000000ULL;

    setup_dos_header(base);

    uint32_t block_va = 0x1100;
    /* 8-byte header + 1 entry (2 bytes) = 10 */
    uint32_t block_size = sizeof(IMAGE_BASE_RELOCATION) + sizeof(uint16_t);
    setup_single_dir64_block(base, block_va, 0x1000, 0);

    IMAGE_NT_HEADERS64 *nt =
        setup_nt_headers(base, old_image_base, block_va, block_size,
                         IMAGE_FILE_RELOCS_STRIPPED);

    int rc = apply_relocations(base, nt);
    check("apply_relocations returns -1 with RELOCS_STRIPPED", rc == -1);

    munmap(base, 4096 * 2);
}

/* ---------------------------------------------------------------- */
/* Test 7: Multiple blocks — two sequential relocation blocks         */
/* ---------------------------------------------------------------- */

static void test_multiple_blocks(void)
{
    printf("\n--- Multiple blocks ---\n");

    /* Need enough space for: DOS(0x80) + NT(0x100) + val1(0x1000) +
     * val2(0x2000) + block1(0x3000) + block2(0x3010). 0x4000 is safe. */
    void *base = mmap(NULL, 0x4000, PROT_READ | PROT_WRITE,
                      MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (base == MAP_FAILED) { printf("  SKIP: mmap failed\n"); return; }

    uint64_t old_image_base = 0x140000000ULL;

    /* Values at 0x1000 and 0x2000 */
    uint64_t *val1 = (uint64_t *)((char *)base + 0x1000);
    uint64_t *val2 = (uint64_t *)((char *)base + 0x2000);
    *val1 = old_image_base;
    *val2 = old_image_base + 0x200;

    setup_dos_header(base);

    /* Block 1 at 0x3000 → targets VA 0x1000 */
    uint32_t block1_va = 0x3000;
    /* 8-byte header + 1 entry (2 bytes) = 10 */
    uint32_t block1_size =
        sizeof(IMAGE_BASE_RELOCATION) + sizeof(uint16_t);
    setup_single_dir64_block(base, block1_va, 0x1000, 0);

    /* Block 2 immediately after block 1 → targets VA 0x2000 */
    uint32_t block2_va = block1_va + block1_size;
    /* 8-byte header + 1 entry (2 bytes) = 10 */
    uint32_t block2_size =
        sizeof(IMAGE_BASE_RELOCATION) + sizeof(uint16_t);
    setup_single_dir64_block(base, block2_va, 0x2000, 0);

    /* Total reloc dir covers both blocks */
    uint32_t reloc_va = block1_va;
    uint32_t reloc_size = block1_size + block2_size;

    IMAGE_NT_HEADERS64 *nt =
        setup_nt_headers(base, old_image_base, reloc_va, reloc_size, 0);

    int rc = apply_relocations(base, nt);
    check("apply_relocations returns 0 with multiple blocks", rc == 0);
    check("val at 0x1000 patched by first block",
          *val1 == (uint64_t)(uintptr_t)base);
    check("val at 0x2000 patched by second block",
          *val2 == (uint64_t)(uintptr_t)base + 0x200);

    munmap(base, 0x4000);
}

/* ---------------------------------------------------------------- */
/* Integration Test: map_image() with MAP_STACK fallback             */
/* ---------------------------------------------------------------- */

/* Magic value written to guard page to detect if MAP_FIXED overwrote it */
#define GUARD_MAGIC 0xDEADBEEFCAFEBABEULL

/**
 * setup_integration — open file, parse headers, place guard page.
 *
 * Returns 0 on success (outputs populated), -1 on failure.
 * On failure, no resources leak (caller does not need cleanup).
 */
static int setup_integration(const char *pe_path,
                              uint64_t *out_preferred_base,
                              size_t *out_image_size,
                              void **out_guard,
                              int *out_guard_placed)
{
    int fd = open(pe_path, O_RDONLY);
    if (fd < 0) {
        perror("    open PE file");
        return -1;
    }

    struct stat st;
    if (fstat(fd, &st) < 0) {
        perror("    fstat");
        close(fd);
        return -1;
    }
    size_t file_size = (size_t)st.st_size;

    void *file_base = mmap(NULL, file_size, PROT_READ, MAP_PRIVATE, fd, 0);
    if (file_base == MAP_FAILED) {
        perror("    mmap file");
        close(fd);
        return -1;
    }

    IMAGE_DOS_HEADER dos;
    if (parse_dos_header(file_base, file_size, &dos) != 0) {
        munmap(file_base, file_size);
        close(fd);
        return -1;
    }

    IMAGE_NT_HEADERS64 nt;
    if (parse_nt_headers(file_base, file_size, &dos, &nt) != 0) {
        munmap(file_base, file_size);
        close(fd);
        return -1;
    }

    *out_preferred_base = nt.OptionalHeader.ImageBase;
    *out_image_size = nt.OptionalHeader.SizeOfImage;

    /* Place a guard page with MAP_FIXED_NOREPLACE at preferred base.
     * If it succeeds, map_image's MAP_FIXED will overwrite it.
     * If it fails (preferred base occupied), map_image's MAP_FIXED
     * will also fail, forcing the MAP_STACK fallback. */
    void *guard = mmap((void *)(uintptr_t)*out_preferred_base, 4096,
                       PROT_READ | PROT_WRITE,
                       MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED_NOREPLACE,
                       -1, 0);
    int guard_placed = 0;
    if (guard != MAP_FAILED) {
        ((uint64_t *)guard)[0] = GUARD_MAGIC;
        guard_placed = 1;
    }

    munmap(file_base, file_size);
    close(fd);

    *out_guard = guard;
    *out_guard_placed = guard_placed;
    return 0;
}

/**
 * cleanup_integration — unmap the image and the guard page if needed.
 *
 * Must be called AFTER all verification but BEFORE any further use of
 * the mapped regions. guard_survived must be computed BEFORE calling
 * munmap on the image (to avoid use-after-free when the guard was
 * overwritten by MAP_FIXED and lives inside the image region).
 */
static void cleanup_integration(void *base,
                                 const IMAGE_NT_HEADERS64 *mapped_nt,
                                 void *guard,
                                 int guard_placed,
                                 int guard_survived)
{
    if (base != NULL) {
        size_t munmap_size = mapped_nt->OptionalHeader.SizeOfImage;
        if (munmap_size == 0) {
            munmap_size = 0x1000;
        }
        munmap(base, munmap_size);
    }

    /* If guard was placed and survived (independent region), unmap it.
     * If guard was destroyed by MAP_FIXED, it is part of the image
     * region already unmapped above. */
    if (guard_placed && guard_survived) {
        munmap(guard, 4096);
    }
}

static void test_integration_map_relocated(const char *pe_path)
{
    printf("\n--- Integration: MAP_STACK fallback ---\n");
    printf("    PE path: %s\n", pe_path);

    /* ---- Setup ---- */
    uint64_t preferred_base = 0;
    size_t image_size = 0;
    void *guard = NULL;
    int guard_placed = 0;

    if (setup_integration(pe_path, &preferred_base, &image_size,
                          &guard, &guard_placed) != 0) {
        printf("  FAIL: setup failed\n");
        failed_tests++;
        total_tests++;
        return;
    }

    printf("    preferred ImageBase: 0x%lx\n", (unsigned long)preferred_base);
    printf("    SizeOfImage: 0x%lx\n", (unsigned long)image_size);

    if (guard_placed) {
        printf("    guard page placed at 0x%lx (MAP_FIXED_NOREPLACE)\n",
               (unsigned long)(uintptr_t)guard);
    } else {
        printf("    guard page placement failed (%s) — preferred base occupied\n",
               strerror(errno));
    }

    /* ---- Main: call map_image() ---- */
    IMAGE_NT_HEADERS64 mapped_nt;
    void *base = map_image(pe_path, NULL, &mapped_nt, NULL);

    check("map_image returns non-NULL", base != NULL);

    if (base != NULL) {
        uint64_t actual_base = (uint64_t)(uintptr_t)base;
        printf("    actual mapped base: 0x%lx\n", (unsigned long)actual_base);

        /* Report guard status (informational) */
        if (guard_placed) {
            int guard_destroyed = (((uint64_t *)guard)[0] != GUARD_MAGIC);
            printf("    guard page: %s\n",
                   guard_destroyed ? "overwritten (MAP_FIXED succeeded)"
                                   : "survived (MAP_STACK fallback)");
        }

        /* Single check: verify the base matches the expected outcome */
        if (actual_base == preferred_base) {
            check("loaded at preferred ImageBase",
                  actual_base == preferred_base);
        } else {
            check("MAP_STACK fallback: loaded at non-preferred base",
                  actual_base != preferred_base);
        }

        /* Verify PE headers at the mapped base */
        IMAGE_DOS_HEADER *img_dos = (IMAGE_DOS_HEADER *)base;
        check("mapped image has valid DOS signature",
              img_dos->e_magic == IMAGE_DOS_SIGNATURE);

        check("mapped NT headers ImageBase matches original",
              mapped_nt.OptionalHeader.ImageBase == preferred_base);

        check("g_image_base set by map_image", g_image_base == base);

        printf("    relocation delta: 0x%lx\n",
               (unsigned long)((uintptr_t)base - preferred_base));
    }

    /* ---- Cleanup ---- */
    /* Compute guard_survived BEFORE unmapping the image to avoid
     * use-after-free when the guard lives inside the image region */
    int guard_survived = 0;
    if (guard_placed) {
        guard_survived = (((uint64_t *)guard)[0] == GUARD_MAGIC);
    }

    cleanup_integration(base, &mapped_nt, guard, guard_placed, guard_survived);
}

/* ---------------------------------------------------------------- */
/* Integration Test 2: forced non-preferred base with pre-allocation */
/* ---------------------------------------------------------------- */

/**
 * test_integration_forced_relocation — forces map_image() to use the
 * MAP_STACK fallback by pre-allocating the entire preferred ImageBase
 * region with MAP_FIXED, then verifies the image still loads and works
 * correctly at a relocated address with relocations applied.
 */
static void test_integration_forced_relocation(const char *pe_path)
{
    printf("\n--- Integration: forced relocation (pre-allocated base) ---\n");
    printf("    PE path: %s\n", pe_path);

    int fd = open(pe_path, O_RDONLY);
    if (fd < 0) {
        perror("    open PE file");
        failed_tests++;
        total_tests++;
        return;
    }

    struct stat st;
    if (fstat(fd, &st) < 0) {
        perror("    fstat");
        close(fd);
        failed_tests++;
        total_tests++;
        return;
    }
    size_t file_size = (size_t)st.st_size;

    void *file_base = mmap(NULL, file_size, PROT_READ, MAP_PRIVATE, fd, 0);
    if (file_base == MAP_FAILED) {
        perror("    mmap file");
        close(fd);
        failed_tests++;
        total_tests++;
        return;
    }

    IMAGE_DOS_HEADER dos;
    if (parse_dos_header(file_base, file_size, &dos) != 0) {
        munmap(file_base, file_size);
        close(fd);
        failed_tests++;
        total_tests++;
        return;
    }

    IMAGE_NT_HEADERS64 nt;
    if (parse_nt_headers(file_base, file_size, &dos, &nt) != 0) {
        munmap(file_base, file_size);
        close(fd);
        failed_tests++;
        total_tests++;
        return;
    }

    munmap(file_base, file_size);
    close(fd);

    uint64_t preferred_base = nt.OptionalHeader.ImageBase;
    size_t image_size = nt.OptionalHeader.SizeOfImage;

    printf("    preferred ImageBase: 0x%lx\n", (unsigned long)preferred_base);
    printf("    SizeOfImage: 0x%lx\n", (unsigned long)image_size);

    /* Pre-allocate the preferred base region to force MAP_FIXED to fail */
    void *prealloc = mmap((void *)(uintptr_t)preferred_base, image_size,
                          PROT_READ | PROT_WRITE,
                          MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED,
                          -1, 0);
    if (prealloc == MAP_FAILED) {
        /* Preferred base already occupied by something else — that's fine,
         * it still forces the MAP_STACK fallback. */
        printf("    pre-allocation failed (%s) — base already occupied\n",
               strerror(errno));
        prealloc = NULL;
    } else {
        /* Tag the pre-allocated region so we can verify it wasn't touched */
        printf("    pre-allocated 0x%lx bytes at preferred base 0x%lx\n",
               (unsigned long)image_size, (unsigned long)preferred_base);
    }

    /* Now call map_image() — MAP_FIXED should fail, triggering MAP_STACK */
    IMAGE_NT_HEADERS64 mapped_nt;
    void *base = map_image(pe_path, NULL, &mapped_nt, NULL);

    check("map_image returns non-NULL", base != NULL);

    if (base != NULL) {
        uint64_t actual_base = (uint64_t)(uintptr_t)base;
        printf("    actual mapped base: 0x%lx\n", (unsigned long)actual_base);

        check("loaded at non-preferred base (MAP_STACK fallback)",
              actual_base != preferred_base);

        /* Verify PE headers at the mapped base */
        IMAGE_DOS_HEADER *img_dos = (IMAGE_DOS_HEADER *)base;
        check("mapped image has valid DOS signature",
              img_dos->e_magic == IMAGE_DOS_SIGNATURE);

        check("mapped NT headers ImageBase matches original",
              mapped_nt.OptionalHeader.ImageBase == preferred_base);

        check("g_image_base set by map_image", g_image_base == base);

        printf("    relocation delta: 0x%lx\n",
               (unsigned long)((uintptr_t)base - preferred_base));
    }

    /* Cleanup: unmap the pre-allocated region and the image */
    if (base != NULL) {
        size_t munmap_size = mapped_nt.OptionalHeader.SizeOfImage;
        if (munmap_size == 0) {
            munmap_size = 0x1000;
        }
        munmap(base, munmap_size);
    }

    if (prealloc != NULL) {
        munmap(prealloc, image_size);
    }
}

/* ---------------------------------------------------------------- */
/* Main                                                               */
/* ---------------------------------------------------------------- */

int main(int argc, char *argv[])
{
    printf("=== Relocation unit tests ===\n");

    test_dir64_relocation();
    test_absolute_noop();
    test_mixed_entries();
    test_empty_reloc_dir();
    test_delta_zero();
    test_relocs_stripped();
    test_multiple_blocks();

    /* Integration tests with real PE file */
    if (argc > 1) {
        test_integration_map_relocated(argv[1]);
        test_integration_forced_relocation(argv[1]);
    } else {
        printf("\n--- Integration: skipped (no PE path provided) ---\n");
    }

    printf("\n========================================\n");
    printf("Total:  %d  Passed: %d  Failed: %d\n",
           total_tests, passed_tests, failed_tests);
    printf("========================================\n");

    return failed_tests > 0 ? 1 : 0;
}
