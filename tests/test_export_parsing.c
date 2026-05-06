/*
 * test_export_parsing.c — Standalone unit test for export table parsing
 *
 * Constructs synthetic PE images with export tables in anonymous memory,
 * then tests parse_export_table, lookup_export, lookup_export_by_ordinal,
 * forwarder detection, and free_export_cache.
 *
 * Build: linked against export_table.o, module_list.o, debug.o, pe_headers.o, pe_imports.o
 */

#define _GNU_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#include "pe.h"
#include "nt_constants.h"
#include "src/loader/module_list.h"
#include "src/loader/export_table.h"

/* ── Test harness ────────────────────────────────────────────── */

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

/* ── Build a PE image in anonymous memory with export dir ──────
 *
 * Layout (all addresses relative to base):
 *   0x0000  DOS header
 *   0x0080  NT headers (PE sig + FileHeader + OptionalHeader)
 *   0x0180  Section headers (2 sections: .text, .rdata)
 *   0x1000  .text section — stub function code
 *   0x2000  .rdata section — export directory + tables + name strings
 *
 * Returns the base address. Caller munmaps with buf_size.
 */
static void *build_pe_with_exports(size_t buf_size,
                                    const char **export_names,
                                    int num_exports,
                                    int has_forwarder,
                                    IMAGE_NT_HEADERS64 **out_nt)
{
    void *base = mmap(NULL, buf_size, PROT_READ | PROT_WRITE,
                      MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (base == MAP_FAILED) return NULL;
    memset(base, 0, buf_size);

    uint8_t *p = (uint8_t *)base;

    /* ── DOS header at 0x0000 ──────────────────────────── */
    IMAGE_DOS_HEADER *dos = (IMAGE_DOS_HEADER *)(p + 0x0000);
    dos->e_magic = IMAGE_DOS_SIGNATURE;
    dos->e_lfanew = 0x80;

    /* ── NT headers at 0x0080 ──────────────────────────── */
    IMAGE_NT_HEADERS64 *nt = (IMAGE_NT_HEADERS64 *)(p + 0x0080);
    nt->Signature = IMAGE_NT_SIGNATURE;
    nt->FileHeader.Machine = IMAGE_FILE_MACHINE_AMD64;
    nt->FileHeader.NumberOfSections = 2;
    nt->FileHeader.SizeOfOptionalHeader = sizeof(IMAGE_OPTIONAL_HEADER64);
    nt->OptionalHeader.Magic = IMAGE_NT_OPTIONAL_HDR64_MAGIC;
    nt->OptionalHeader.SectionAlignment = 0x1000;
    nt->OptionalHeader.FileAlignment = 0x200;
    nt->OptionalHeader.SizeOfImage = 0x3000;
    nt->OptionalHeader.DataDirectory[DIRECTORY_ENTRY_EXPORT].VirtualAddress = 0x2000;
    nt->OptionalHeader.DataDirectory[DIRECTORY_ENTRY_EXPORT].Size = 0x200;

    if (out_nt) *out_nt = nt;

    /* ── Section headers at 0x0080 + sizeof(NT) ───────── */
    size_t sec_off = 0x80 + sizeof(uint32_t) + sizeof(IMAGE_FILE_HEADER) + sizeof(IMAGE_OPTIONAL_HEADER64);
    IMAGE_SECTION_HEADER *sec = (IMAGE_SECTION_HEADER *)(p + sec_off);

    /* .text at RVA 0x1000 */
    memcpy(sec[0].Name, ".text\0\0\0", 8);
    sec[0].Misc.VirtualSize = 0x1000;
    sec[0].VirtualAddress = 0x1000;
    sec[0].SizeOfRawData = 0x1000;
    sec[0].Characteristics = IMAGE_SCN_MEM_READ | IMAGE_SCN_MEM_EXECUTE;

    /* .rdata at RVA 0x2000 */
    memcpy(sec[1].Name, ".rdata\0\0", 8);
    sec[1].Misc.VirtualSize = 0x1000;
    sec[1].VirtualAddress = 0x2000;
    sec[1].SizeOfRawData = 0x1000;
    sec[1].Characteristics = IMAGE_SCN_MEM_READ;

    /* ── Stub functions at RVA 0x1000 ──────────────────── */
    for (int i = 0; i < num_exports; i++) {
        uint32_t rva = 0x1000 + i * 0x10;
        p[rva] = 0xC3; /* ret */
    }

    /* ── Export directory at RVA 0x2000 ────────────────── */
    IMAGE_EXPORT_DIRECTORY *exp = (IMAGE_EXPORT_DIRECTORY *)(p + 0x2000);
    exp->NumberOfFunctions = (uint32_t)num_exports;
    exp->NumberOfNames = (uint32_t)num_exports;
    exp->Base = 1;

    /* Layout inside .rdata (0x2000+):
     *  0x0000  IMAGE_EXPORT_DIRECTORY (40 bytes)
     *  0x0028  "TEST.DLL\0"
     *  0x0030  AddressOfNames (num_exports * 4 bytes)
     *  +names  AddressOfNameOrdinals (num_exports * 2 bytes)
     *  +ords   AddressOfFunctions (num_exports * 4 bytes)
     *  +funcs  Name strings (variable length)
     */
    uint32_t name_table_rva = 0x2030;
    uint32_t ordinal_rva = name_table_rva + num_exports * sizeof(uint32_t);
    uint32_t func_rva = ordinal_rva + num_exports * sizeof(uint16_t);
    uint32_t name_str_rva = func_rva + num_exports * sizeof(uint32_t);

    exp->AddressOfNames = name_table_rva;
    exp->AddressOfNameOrdinals = ordinal_rva;
    exp->AddressOfFunctions = func_rva;
    exp->Name = 0x2028;

    /* DLL name string */
    memcpy(p + 0x2028, "TEST.DLL\0", 9);

    /* AddressOfNames array (sorted by name — binary search requires this) */
    uint32_t *names = (uint32_t *)(p + name_table_rva);
    uint32_t cur_name_rva = name_str_rva;
    for (int i = 0; i < num_exports; i++) {
        names[i] = cur_name_rva;
        cur_name_rva += (uint32_t)(strlen(export_names[i]) + 1);
    }

    /* AddressOfNameOrdinals — stores index into AddressOfFunctions */
    uint16_t *ordinals = (uint16_t *)(p + ordinal_rva);
    for (int i = 0; i < num_exports; i++) {
        ordinals[i] = (uint16_t)i; /* 0-based index into func table */
    }

    /* AddressOfFunctions */
    uint32_t *funcs = (uint32_t *)(p + func_rva);
    for (int i = 0; i < num_exports; i++) {
        if (has_forwarder && (i == num_exports - 1)) {
            /* Forwarder: RVA points into the export directory itself */
            funcs[i] = 0x2080; /* points to the forwarder string we write below */
        } else {
            funcs[i] = 0x1000 + i * 0x10;
        }
    }

    /* Name strings */
    uint8_t *str_pos = p + name_str_rva;
    for (int i = 0; i < num_exports; i++) {
        memcpy(str_pos, export_names[i], strlen(export_names[i]) + 1);
        str_pos += strlen(export_names[i]) + 1;
    }

    /* Forwarder string — placed at 0x2080 to avoid overlapping the name table at 0x2030+ */
    if (has_forwarder) {
        /* Put forwarder string at a safe location within the export dir that
         * doesn't overlap with the name/ordinal/func tables or name strings. */
        memcpy(p + 0x2080, "kernel32.dll!SomeFunc\0", 23);
    }

    return base;
}

/* ── Test: parse_export_table ────────────────────────────────── */

static void test_parse_export_table(void)
{
    printf("\n=== parse_export_table ===\n");

    const char *names[] = { "Alpha", "Beta", "Delta", "Gamma" }; /* sorted for binary search */
    size_t buf_size = 0x3000;

    IMAGE_NT_HEADERS64 *nt = NULL;
    void *base = build_pe_with_exports(buf_size, names, 4, 0, &nt);
    if (!base) { printf("  SKIP: build failed\n"); return; }

    EXPORT_CACHE *cache = parse_export_table(base, nt);
    check("parse_export_table returns non-NULL", cache != NULL);

    if (cache) {
        check("number_of_functions == 4", cache->number_of_functions == 4);
        check("number_of_names == 4", cache->number_of_names == 4);
        check("base_ordinal == 1", cache->base_ordinal == 1);
        check("name_table allocated", cache->name_table != NULL);
        check("ordinal_table allocated", cache->ordinal_table != NULL);
        check("func_table allocated", cache->func_table != NULL);
    }

    if (cache) free_export_cache(cache);
    munmap(base, buf_size);
}

/* ── Test: parse_export_table with no export dir ─────────────── */

static void test_parse_no_export(void)
{
    printf("\n=== parse_export_table (no export dir) ===\n");

    size_t buf_size = 0x1000;
    void *base = mmap(NULL, buf_size, PROT_READ | PROT_WRITE,
                      MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (base == MAP_FAILED) return;
    memset(base, 0, buf_size);

    IMAGE_DOS_HEADER *dos = (IMAGE_DOS_HEADER *)base;
    dos->e_magic = IMAGE_DOS_SIGNATURE;
    dos->e_lfanew = 64;

    IMAGE_NT_HEADERS64 *nt = (IMAGE_NT_HEADERS64 *)((uint8_t *)base + 64);
    nt->Signature = IMAGE_NT_SIGNATURE;
    nt->FileHeader.Machine = IMAGE_FILE_MACHINE_AMD64;
    nt->FileHeader.NumberOfSections = 0;
    nt->FileHeader.SizeOfOptionalHeader = sizeof(IMAGE_OPTIONAL_HEADER64);
    nt->OptionalHeader.Magic = IMAGE_NT_OPTIONAL_HDR64_MAGIC;
    nt->OptionalHeader.DataDirectory[DIRECTORY_ENTRY_EXPORT].VirtualAddress = 0;

    EXPORT_CACHE *cache = parse_export_table(base, nt);
    check("returns NULL for no export dir", cache == NULL);

    munmap(base, buf_size);
}

/* ── Test: lookup_export (binary search) ─────────────────────── */

static void test_lookup_export(void)
{
    printf("\n=== lookup_export ===\n");

    const char *names[] = { "Alpha", "Beta", "Delta", "Gamma" }; /* sorted for binary search */
    size_t buf_size = 0x3000;

    IMAGE_NT_HEADERS64 *nt = NULL;
    void *base = build_pe_with_exports(buf_size, names, 4, 0, &nt);
    if (!base) { printf("  SKIP\n"); return; }

    EXPORT_CACHE *cache = parse_export_table(base, nt);
    if (!cache) { munmap(base, buf_size); printf("  SKIP: parse failed\n"); return; }

    loaded_module_t mod;
    memset(&mod, 0, sizeof(mod));
    mod.base = base;
    mod.export_cache = cache;

    /* Lookup each name */
    for (int i = 0; i < 4; i++) {
        char label[64];
        snprintf(label, sizeof(label), "lookup_export finds %s", names[i]);
        void *addr = lookup_export(&mod, names[i]);
        check(label, addr != NULL);
        if (addr) {
            check("address in .text region",
                  (uintptr_t)addr >= (uintptr_t)base + 0x1000 &&
                  (uintptr_t)addr < (uintptr_t)base + 0x2000);
        }
    }

    /* Non-existent name */
    void *missing = lookup_export(&mod, "NonExistent");
    check("returns NULL for missing name", missing == NULL);

    /* NULL module */
    check("returns NULL for NULL module", lookup_export(NULL, "Alpha") == NULL);

    free_export_cache(cache);
    munmap(base, buf_size);
}

/* ── Test: lookup_export_by_ordinal ──────────────────────────── */

static void test_lookup_export_by_ordinal(void)
{
    printf("\n=== lookup_export_by_ordinal ===\n");

    const char *names[] = { "Alpha", "Beta", "Gamma" };
    size_t buf_size = 0x3000;

    IMAGE_NT_HEADERS64 *nt = NULL;
    void *base = build_pe_with_exports(buf_size, names, 3, 0, &nt);
    if (!base) { printf("  SKIP\n"); return; }

    EXPORT_CACHE *cache = parse_export_table(base, nt);
    if (!cache) { munmap(base, buf_size); printf("  SKIP\n"); return; }

    loaded_module_t mod;
    memset(&mod, 0, sizeof(mod));
    mod.base = base;
    mod.export_cache = cache;

    /* ordinal 1 (base=1, index 0) */
    void *a1 = lookup_export_by_ordinal(&mod, 1);
    check("ordinal 1 returns non-NULL", a1 != NULL);

    /* ordinal 3 (index 2) */
    void *a3 = lookup_export_by_ordinal(&mod, 3);
    check("ordinal 3 returns non-NULL", a3 != NULL);

    /* out of range */
    check("ordinal 100 returns NULL", lookup_export_by_ordinal(&mod, 100) == NULL);

    /* below base */
    check("ordinal 0 returns NULL", lookup_export_by_ordinal(&mod, 0) == NULL);

    /* NULL module */
    check("NULL module returns NULL", lookup_export_by_ordinal(NULL, 1) == NULL);

    free_export_cache(cache);
    munmap(base, buf_size);
}

/* ── Test: forwarder detection ───────────────────────────────── */

static void test_forwarder_detection(void)
{
    printf("\n=== Forwarder Detection ===\n");

    const char *names[] = { "Alpha", "Beta", "Forwarded" };
    size_t buf_size = 0x3000;

    IMAGE_NT_HEADERS64 *nt = NULL;
    void *base = build_pe_with_exports(buf_size, names, 3, 1, &nt); /* has_forwarder=1 */
    if (!base) { printf("  SKIP\n"); return; }

    EXPORT_CACHE *cache = parse_export_table(base, nt);
    if (!cache) { munmap(base, buf_size); printf("  SKIP\n"); return; }

    loaded_module_t mod;
    memset(&mod, 0, sizeof(mod));
    mod.base = base;
    mod.export_cache = cache;

    /* "Forwarded" has func_rva pointing into export dir -> should be NULL */
    void *fwd = lookup_export(&mod, "Forwarded");
    check("lookup_export returns NULL for forwarder", fwd == NULL);

    /* "Alpha" should still work */
    void *alpha = lookup_export(&mod, "Alpha");
    check("non-forwarder lookup still works", alpha != NULL);

    /* ordinal lookup for forwarded function (ordinal 3 = index 2) */
    void *fwd_ord = lookup_export_by_ordinal(&mod, 3);
    check("lookup_by_ordinal returns NULL for forwarder", fwd_ord == NULL);

    free_export_cache(cache);
    munmap(base, buf_size);
}

/* ── Test: free_export_cache ─────────────────────────────────── */

static void test_free_export_cache(void)
{
    printf("\n=== free_export_cache ===\n");

    const char *names[] = { "A", "B" };
    size_t buf_size = 0x3000;

    IMAGE_NT_HEADERS64 *nt = NULL;
    void *base = build_pe_with_exports(buf_size, names, 2, 0, &nt);
    if (!base) { printf("  SKIP\n"); return; }

    EXPORT_CACHE *cache = parse_export_table(base, nt);
    if (!cache) { munmap(base, buf_size); printf("  SKIP\n"); return; }

    free_export_cache(cache);
    check("free does not crash", 1);

    free_export_cache(NULL);
    check("free(NULL) does not crash", 1);

    munmap(base, buf_size);
}

/* ── Main ────────────────────────────────────────────────────── */

int main(void)
{
    printf("=== Export Parsing Tests ===\n");

    test_parse_export_table();
    test_parse_no_export();
    test_lookup_export();
    test_lookup_export_by_ordinal();
    test_forwarder_detection();
    test_free_export_cache();

    printf("\n========================================\n");
    printf("Total:  %d  Passed: %d  Failed: %d\n",
           total_tests, passed_tests, failed_tests);
    printf("========================================\n");

    return failed_tests > 0 ? 1 : 0;
}
