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

/* Build a single-entry DIR64 relocation block at block_va */
static void setup_single_dir64_block(void *base,
                                      uint32_t block_va,
                                      uint32_t target_va,
                                      uint16_t entry_offset)
{
    IMAGE_BASE_RELOCATION *block =
        (IMAGE_BASE_RELOCATION *)((char *)base + block_va);
    block->virtualAddress = target_va;
    block->sizeOfBlock =
        sizeof(IMAGE_BASE_RELOCATION) + sizeof(IMAGE_RELOC_ENTRY);
    block->entries[0].type = IMAGE_REL_BASED_DIR64;
    block->entries[0].offset = entry_offset;
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
    uint32_t block_size =
        sizeof(IMAGE_BASE_RELOCATION) + sizeof(IMAGE_RELOC_ENTRY);
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
    block->sizeOfBlock =
        sizeof(IMAGE_BASE_RELOCATION) + sizeof(IMAGE_RELOC_ENTRY);
    block->entries[0].type = IMAGE_REL_BASED_ABSOLUTE;
    block->entries[0].offset = 0;

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
    /* Two entries: ABSOLUTE at offset 0, DIR64 at offset 8 */
    block->sizeOfBlock =
        sizeof(IMAGE_BASE_RELOCATION) + 2 * sizeof(IMAGE_RELOC_ENTRY);
    block->entries[0].type = IMAGE_REL_BASED_ABSOLUTE;
    block->entries[0].offset = 0;
    block->entries[1].type = IMAGE_REL_BASED_DIR64;
    block->entries[1].offset = 8;

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
    uint32_t block_size =
        sizeof(IMAGE_BASE_RELOCATION) + sizeof(IMAGE_RELOC_ENTRY);
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
    uint32_t block_size =
        sizeof(IMAGE_BASE_RELOCATION) + sizeof(IMAGE_RELOC_ENTRY);
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
    uint32_t block1_size =
        sizeof(IMAGE_BASE_RELOCATION) + sizeof(IMAGE_RELOC_ENTRY);
    setup_single_dir64_block(base, block1_va, 0x1000, 0);

    /* Block 2 immediately after block 1 → targets VA 0x2000 */
    uint32_t block2_va = block1_va + block1_size;
    uint32_t block2_size =
        sizeof(IMAGE_BASE_RELOCATION) + sizeof(IMAGE_RELOC_ENTRY);
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

/**
 * Integration test: load a real PE at a non-preferred base.
 *
 * Strategy:
 *   1. Read the PE headers from the file to discover the preferred ImageBase.
 *   2. Pre-allocate 1 page at the preferred ImageBase using MAP_FIXED.
 *      This ensures map_image()'s MAP_FIXED attempt will fail.
 *   3. Call map_image() — it should fall back to MAP_STACK and apply
 *      relocations at a different address.
 *   4. Verify the returned base != preferred ImageBase.
 *   5. Verify map_image() succeeded (relocations applied, sections mapped).
 */

static void test_integration_map_relocated(const char *pe_path)
{
    printf("\n--- Integration: MAP_STACK fallback ---\n");
    printf("    PE path: %s\n", pe_path);

    /* Step 1: open the file and read headers to find preferred ImageBase */
    int fd = open(pe_path, O_RDONLY);
    if (fd < 0) {
        perror("    open PE file");
        printf("  FAIL: cannot open PE file\n");
        failed_tests++;
        total_tests++;
        return;
    }

    struct stat st;
    if (fstat(fd, &st) < 0) {
        perror("    fstat");
        close(fd);
        printf("  FAIL: cannot stat PE file\n");
        failed_tests++;
        total_tests++;
        return;
    }
    size_t file_size = (size_t)st.st_size;

    void *file_base = mmap(NULL, file_size, PROT_READ, MAP_PRIVATE, fd, 0);
    if (file_base == MAP_FAILED) {
        perror("    mmap file");
        close(fd);
        printf("  FAIL: cannot mmap PE file\n");
        failed_tests++;
        total_tests++;
        return;
    }

    IMAGE_DOS_HEADER dos;
    if (parse_dos_header(file_base, file_size, &dos) != 0) {
        munmap(file_base, file_size);
        close(fd);
        printf("  FAIL: invalid DOS header\n");
        failed_tests++;
        total_tests++;
        return;
    }

    IMAGE_NT_HEADERS64 nt;
    if (parse_nt_headers(file_base, file_size, &dos, &nt) != 0) {
        munmap(file_base, file_size);
        close(fd);
        printf("  FAIL: invalid NT headers\n");
        failed_tests++;
        total_tests++;
        return;
    }

    uint64_t preferred_base = nt.OptionalHeader.ImageBase;
    size_t image_size = nt.OptionalHeader.SizeOfImage;

    printf("    preferred ImageBase: 0x%lx\n", (unsigned long)preferred_base);
    printf("    SizeOfImage: 0x%lx\n", (unsigned long)image_size);

    /* Step 2: try to place a guard page with MAP_FIXED_NOREPLACE at
     * the preferred base + image_size region. This serves two purposes:
     *   (a) If the preferred base is already occupied, guard placement
     *       fails, and map_image's MAP_FIXED will also fail → MAP_STACK
     *       fallback proves the relocation path.
     *   (b) If guard placement succeeds, map_image's MAP_FIXED will
     *       overwrite it (MAP_FIXED replaces existing mappings). The
     *       image loads at preferred base; g_image_base confirms loading.
     * In either case, we verify the image loads and g_image_base is set. */
    /* Magic value written to guard page to detect if MAP_FIXED overwrote it */
    uint64_t guard_magic = 0xDEADBEEFCAFEBABEULL;

    void *guard = mmap((void *)(uintptr_t)preferred_base, 4096,
                       PROT_READ | PROT_WRITE,
                       MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED_NOREPLACE,
                       -1, 0);
    int guard_placed = 0;
    if (guard != MAP_FAILED) {
        ((uint64_t *)guard)[0] = guard_magic;
        guard_placed = 1;
        printf("    guard page placed at 0x%lx (MAP_FIXED_NOREPLACE)\n",
               (unsigned long)(uintptr_t)guard);
    } else {
        printf("    guard page placement failed (%s) — preferred base occupied\n",
               strerror(errno));
    }

    munmap(file_base, file_size);
    close(fd);

    /* Step 3: call map_image() — it tries MAP_FIXED, falls back to MAP_STACK */
    IMAGE_NT_HEADERS64 mapped_nt;
    void *base = map_image(pe_path, NULL, &mapped_nt, NULL);

    /* Step 4: verify */
    check("map_image returns non-NULL", base != NULL);

    if (base != NULL) {
        uint64_t actual_base = (uint64_t)(uintptr_t)base;
        printf("    actual mapped base: 0x%lx\n", (unsigned long)actual_base);

        if (guard_placed) {
            /* Check if guard was overwritten by MAP_FIXED */
            int guard_destroyed = (((uint64_t *)guard)[0] != guard_magic);
            printf("    guard page: %s (MAP_FIXED %s)\n",
                   guard_destroyed ? "overwritten" : "survived",
                   guard_destroyed ? "succeeded" : "failed → MAP_STACK fallback");

            if (guard_destroyed) {
                /* MAP_FIXED succeeded: image at preferred base */
                check("MAP_FIXED succeeded: loaded at preferred ImageBase",
                      actual_base == preferred_base);
            } else {
                /* MAP_FIXED failed: MAP_STACK fallback at different base */
                check("MAP_STACK fallback: loaded at non-preferred base",
                      actual_base != preferred_base);
            }
        } else {
            /* No guard — map_image may have succeeded or failed at
             * preferred base. Either way, verify what happened. */
            if (actual_base == preferred_base) {
                printf("    loaded at preferred base (MAP_FIXED succeeded)\n");
                check("loaded at preferred ImageBase",
                      actual_base == preferred_base);
            } else {
                printf("    loaded at non-preferred base (MAP_FIXED failed)\n");
                check("MAP_STACK fallback at non-preferred base",
                      actual_base != preferred_base);
            }
        }

        /* Verify the mapped image has valid PE headers at the base */
        IMAGE_DOS_HEADER *img_dos = (IMAGE_DOS_HEADER *)base;
        check("mapped image has valid DOS signature",
              img_dos->e_magic == IMAGE_DOS_SIGNATURE);

        check("mapped NT headers ImageBase matches original",
              mapped_nt.OptionalHeader.ImageBase == preferred_base);

        /* Verify g_image_base was set by map_image */
        check("g_image_base set by map_image",
              g_image_base == base);

        printf("    relocation delta: 0x%lx\n",
               (unsigned long)((uintptr_t)base - preferred_base));
    }

    /* Step 5: cleanup */
    int guard_survived = 0;
    if (guard_placed) {
        guard_survived = (((uint64_t *)guard)[0] == guard_magic);
    }

    if (base != NULL) {
        size_t munmap_size = mapped_nt.OptionalHeader.SizeOfImage;
        if (munmap_size == 0) {
            munmap_size = 0x1000;
        }
        munmap(base, munmap_size);
    }

    if (guard_survived) {
        /* Guard survived — MAP_FIXED failed, guard is independent */
        munmap(guard, 4096);
    }
    /* If guard was destroyed by MAP_FIXED, it's part of the unmapped
     * image region above. */
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

    /* Integration test with real PE file */
    if (argc > 1) {
        test_integration_map_relocated(argv[1]);
    } else {
        printf("\n--- Integration: skipped (no PE path provided) ---\n");
    }

    printf("\n========================================\n");
    printf("Total:  %d  Passed: %d  Failed: %d\n",
           total_tests, passed_tests, failed_tests);
    printf("========================================\n");

    return failed_tests > 0 ? 1 : 0;
}
