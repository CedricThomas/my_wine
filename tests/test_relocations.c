/*
 * test_relocations.c — standalone unit test for apply_relocations()
 *
 * Crafts a minimal PE image in memory with known DIR64 relocation
 * entries, calls apply_relocations(), and verifies the patched values.
 *
 * Usage: ./build/test_relocations
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <sys/mman.h>

#include "pe.h"
#include "nt_constants.h"

/* Forward declare — relocations.c is compiled into this test binary */
int apply_relocations(void *base, IMAGE_NT_HEADERS64 *nt);

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
/* Main                                                               */
/* ---------------------------------------------------------------- */

int main(void)
{
    printf("=== Relocation unit tests ===\n");

    test_dir64_relocation();
    test_absolute_noop();
    test_mixed_entries();
    test_empty_reloc_dir();
    test_delta_zero();
    test_relocs_stripped();
    test_multiple_blocks();

    printf("\n========================================\n");
    printf("Total:  %d  Passed: %d  Failed: %d\n",
           total_tests, passed_tests, failed_tests);
    printf("========================================\n");

    return failed_tests > 0 ? 1 : 0;
}
