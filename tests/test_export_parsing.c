/*
 * test_export_parsing.c — Standalone unit test for export table parsing
 *
 * Constructs synthetic PE images with export tables in anonymous memory,
 * then tests parse_export_table, lookup_export, lookup_export_by_ordinal,
 * forwarder detection, and reset_export_cache.
 *
 * The export cache is now embedded in loaded_module_t. For testing, we
 * create a minimal loaded_module_t on the stack.
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
#include "src/pe_priv.h"

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

/* Create a minimal loaded_module_t on the stack for testing.
 * The embedded export_cache is zero'd. */
static loaded_module_t *make_test_mod(void *base, IMAGE_NT_HEADERS *nt)
{
    static loaded_module_t mod;  /* static to avoid stack overflow with embedded arrays */
    memset(&mod, 0, sizeof(mod));
    mod.base = base;
    mod.nt = nt;
    return &mod;
}

/* ── Build a PE image in anonymous memory with export dir ────── */
static void *build_pe_with_exports(size_t buf_size,
                                    const char **export_names,
                                    int num_exports,
                                    int has_forwarder,
                                    IMAGE_NT_HEADERS **out_nt)
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
    /* Write as IMAGE_NT_HEADERS (tagged union) in buffer */
    IMAGE_NT_HEADERS *nt = (IMAGE_NT_HEADERS *)(p + 0x0080);
    memset(nt, 0, sizeof(IMAGE_NT_HEADERS));
    nt->u.nt64.Signature = IMAGE_NT_SIGNATURE;
    nt->u.nt64.FileHeader.Machine = IMAGE_FILE_MACHINE_AMD64;
    nt->u.nt64.FileHeader.NumberOfSections = 2;
    nt->u.nt64.FileHeader.SizeOfOptionalHeader = sizeof(IMAGE_OPTIONAL_HEADER64);
    nt->u.nt64.OptionalHeader.Magic = IMAGE_NT_OPTIONAL_HDR64_MAGIC;
    nt->u.nt64.OptionalHeader.SectionAlignment = 0x1000;
    nt->u.nt64.OptionalHeader.FileAlignment = 0x200;
    nt->u.nt64.OptionalHeader.SizeOfImage = 0x3000;
    nt->u.nt64.OptionalHeader.NumberOfRvaAndSizes = IMAGE_NUMBEROF_DIRECTORY_ENTRIES;
    nt->u.nt64.OptionalHeader.DataDirectory[DIRECTORY_ENTRY_EXPORT].VirtualAddress = 0x2000;
    nt->u.nt64.OptionalHeader.DataDirectory[DIRECTORY_ENTRY_EXPORT].Size = 0x200;
    nt->pe_type = PE_TYPE_64;

    if (out_nt) *out_nt = nt;

    /* ── Section headers ───────────────────────────────── */
    size_t sec_off = 0x80 + sizeof(uint32_t) + sizeof(IMAGE_FILE_HEADER) + sizeof(IMAGE_OPTIONAL_HEADER64);
    IMAGE_SECTION_HEADER *sec = (IMAGE_SECTION_HEADER *)(p + sec_off);

    memcpy(sec[0].Name, ".text\0\0\0", 8);
    sec[0].Misc.VirtualSize = 0x1000;
    sec[0].VirtualAddress = 0x1000;
    sec[0].SizeOfRawData = 0x1000;
    sec[0].Characteristics = IMAGE_SCN_MEM_READ | IMAGE_SCN_MEM_EXECUTE;

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

    uint32_t name_table_rva = 0x2030;
    uint32_t ordinal_rva = name_table_rva + num_exports * sizeof(uint32_t);
    uint32_t func_rva = ordinal_rva + num_exports * sizeof(uint16_t);
    uint32_t name_str_rva = func_rva + num_exports * sizeof(uint32_t);

    exp->AddressOfNames = name_table_rva;
    exp->AddressOfNameOrdinals = ordinal_rva;
    exp->AddressOfFunctions = func_rva;
    exp->Name = 0x2028;

    memcpy(p + 0x2028, "TEST.DLL\0", 9);

    uint32_t *names = (uint32_t *)(p + name_table_rva);
    uint32_t cur_name_rva = name_str_rva;
    for (int i = 0; i < num_exports; i++) {
        names[i] = cur_name_rva;
        cur_name_rva += (uint32_t)(strlen(export_names[i]) + 1);
    }

    uint16_t *ordinals = (uint16_t *)(p + ordinal_rva);
    for (int i = 0; i < num_exports; i++) {
        ordinals[i] = (uint16_t)i;
    }

    uint32_t *funcs = (uint32_t *)(p + func_rva);
    for (int i = 0; i < num_exports; i++) {
        if (has_forwarder && (i == num_exports - 1)) {
            funcs[i] = 0x2080;
        } else {
            funcs[i] = 0x1000 + i * 0x10;
        }
    }

    uint8_t *str_pos = p + name_str_rva;
    for (int i = 0; i < num_exports; i++) {
        memcpy(str_pos, export_names[i], strlen(export_names[i]) + 1);
        str_pos += strlen(export_names[i]) + 1;
    }

    if (has_forwarder) {
        memcpy(p + 0x2080, "kernel32.dll!SomeFunc\0", 23);
    }

    return base;
}

/* ── Test: parse_export_table ────────────────────────────────── */

static void test_parse_export_table(void)
{
    printf("\n=== parse_export_table ===\n");

    const char *names[] = { "Alpha", "Beta", "Delta", "Gamma" };
    size_t buf_size = 0x3000;

    IMAGE_NT_HEADERS *nt = NULL;
    void *base = build_pe_with_exports(buf_size, names, 4, 0, &nt);
    if (!base) { printf("  SKIP: build failed\n"); return; }

    loaded_module_t *mod = make_test_mod(base, nt);
    int rc = parse_export_table(mod);
    check("parse_export_table returns 0", rc == 0);

    EXPORT_CACHE *cache = &mod->export_cache;
    check("number_of_functions == 4", cache->number_of_functions == 4);
    check("number_of_names == 4", cache->number_of_names == 4);
    check("base_ordinal == 1", cache->base_ordinal == 1);
    check("name_table has data", cache->name_table[0] != 0);
    check("ordinal_table has data", cache->ordinal_table[1] == 1);
    check("func_table has data", cache->func_table[0] != 0);

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

    /* Write as IMAGE_NT_HEADERS in buffer */
    IMAGE_NT_HEADERS *nt = (IMAGE_NT_HEADERS *)((uint8_t *)base + 64);
    memset(nt, 0, sizeof(IMAGE_NT_HEADERS));
    nt->u.nt64.Signature = IMAGE_NT_SIGNATURE;
    nt->u.nt64.FileHeader.Machine = IMAGE_FILE_MACHINE_AMD64;
    nt->u.nt64.FileHeader.NumberOfSections = 0;
    nt->u.nt64.FileHeader.SizeOfOptionalHeader = sizeof(IMAGE_OPTIONAL_HEADER64);
    nt->u.nt64.OptionalHeader.Magic = IMAGE_NT_OPTIONAL_HDR64_MAGIC;
    nt->u.nt64.OptionalHeader.DataDirectory[DIRECTORY_ENTRY_EXPORT].VirtualAddress = 0;
    nt->pe_type = PE_TYPE_64;

    loaded_module_t *mod = make_test_mod(base, nt);
    int rc = parse_export_table(mod);
    check("returns -1 for no export dir", rc == -1);

    munmap(base, buf_size);
}

/* ── Test: malformed export arrays outside image bounds ───────── */

static void test_parse_malformed_export_bounds(void)
{
    printf("\n=== parse_export_table (malformed bounds) ===\n");

    const char *names[] = { "Alpha" };
    size_t buf_size = 0x3000;

    IMAGE_NT_HEADERS *nt = NULL;
    void *base = build_pe_with_exports(buf_size, names, 1, 0, &nt);
    if (!base) { printf("  SKIP\n"); return; }

    IMAGE_EXPORT_DIRECTORY *exp = (IMAGE_EXPORT_DIRECTORY *)((uint8_t *)base + 0x2000);
    exp->AddressOfNames = 0x2fff; /* one byte before end, too small for uint32_t */

    loaded_module_t *mod = make_test_mod(base, nt);
    int rc = parse_export_table(mod);
    check("parse_export_table rejects out-of-bounds name table", rc == -1);
    check("export cache remains empty after malformed parse",
          mod->export_cache.number_of_names == 0);

    munmap(base, buf_size);
}

static void test_lookup_malformed_export_name(void)
{
    printf("\n=== lookup_export (malformed name RVA) ===\n");

    const char *names[] = { "Alpha" };
    size_t buf_size = 0x3000;

    IMAGE_NT_HEADERS *nt = NULL;
    void *base = build_pe_with_exports(buf_size, names, 1, 0, &nt);
    if (!base) { printf("  SKIP\n"); return; }

    loaded_module_t *mod = make_test_mod(base, nt);
    parse_export_table(mod);
    mod->export_cache.name_table[0] = 0x2fff; /* no room for a bounded string */

    check("lookup_export rejects malformed name RVA",
          lookup_export(mod, "Alpha") == NULL);

    munmap(base, buf_size);
}

/* ── Test: lookup_export (binary search) ─────────────────────── */

static void test_lookup_export(void)
{
    printf("\n=== lookup_export ===\n");

    const char *names[] = { "Alpha", "Beta", "Delta", "Gamma" };
    size_t buf_size = 0x3000;

    IMAGE_NT_HEADERS *nt = NULL;
    void *base = build_pe_with_exports(buf_size, names, 4, 0, &nt);
    if (!base) { printf("  SKIP\n"); return; }

    loaded_module_t *mod = make_test_mod(base, nt);
    parse_export_table(mod);

    for (int i = 0; i < 4; i++) {
        char label[64];
        snprintf(label, sizeof(label), "lookup_export finds %s", names[i]);
        void *addr = lookup_export(mod, names[i]);
        check(label, addr != NULL);
        if (addr) {
            check("address in .text region",
                  (uintptr_t)addr >= (uintptr_t)base + 0x1000 &&
                  (uintptr_t)addr < (uintptr_t)base + 0x2000);
        }
    }

    void *missing = lookup_export(mod, "NonExistent");
    check("returns NULL for missing name", missing == NULL);

    check("returns NULL for NULL module", lookup_export(NULL, "Alpha") == NULL);

    munmap(base, buf_size);
}

/* ── Test: lookup_export_by_ordinal ──────────────────────────── */

static void test_lookup_export_by_ordinal(void)
{
    printf("\n=== lookup_export_by_ordinal ===\n");

    const char *names[] = { "Alpha", "Beta", "Gamma" };
    size_t buf_size = 0x3000;

    IMAGE_NT_HEADERS *nt = NULL;
    void *base = build_pe_with_exports(buf_size, names, 3, 0, &nt);
    if (!base) { printf("  SKIP\n"); return; }

    loaded_module_t *mod = make_test_mod(base, nt);
    parse_export_table(mod);

    void *a1 = lookup_export_by_ordinal(mod, 1);
    check("ordinal 1 returns non-NULL", a1 != NULL);

    void *a3 = lookup_export_by_ordinal(mod, 3);
    check("ordinal 3 returns non-NULL", a3 != NULL);

    check("ordinal 100 returns NULL", lookup_export_by_ordinal(mod, 100) == NULL);
    check("ordinal 0 returns NULL", lookup_export_by_ordinal(mod, 0) == NULL);
    check("NULL module returns NULL", lookup_export_by_ordinal(NULL, 1) == NULL);

    munmap(base, buf_size);
}

/* ── Test: forwarder detection ───────────────────────────────── */

static void test_forwarder_detection(void)
{
    printf("\n=== Forwarder Detection ===\n");

    const char *names[] = { "Alpha", "Beta", "Forwarded" };
    size_t buf_size = 0x3000;

    IMAGE_NT_HEADERS *nt = NULL;
    void *base = build_pe_with_exports(buf_size, names, 3, 1, &nt);
    if (!base) { printf("  SKIP\n"); return; }

    loaded_module_t *mod = make_test_mod(base, nt);
    parse_export_table(mod);

    void *fwd = lookup_export(mod, "Forwarded");
    check("lookup_export returns NULL for forwarder", fwd == NULL);

    void *alpha = lookup_export(mod, "Alpha");
    check("non-forwarder lookup still works", alpha != NULL);

    void *fwd_ord = lookup_export_by_ordinal(mod, 3);
    check("lookup_by_ordinal returns NULL for forwarder", fwd_ord == NULL);

    munmap(base, buf_size);
}

/* ── Test: reset_export_cache ────────────────────────────────── */

static void test_reset_export_cache(void)
{
    printf("\n=== reset_export_cache ===\n");

    const char *names[] = { "A", "B" };
    size_t buf_size = 0x3000;

    IMAGE_NT_HEADERS *nt = NULL;
    void *base = build_pe_with_exports(buf_size, names, 2, 0, &nt);
    if (!base) { printf("  SKIP\n"); return; }

    loaded_module_t *mod = make_test_mod(base, nt);
    parse_export_table(mod);
    check("parse_export_table populated cache", mod->export_cache.number_of_names == 2);

    reset_export_cache(mod);
    check("reset_export_cache zeroes name count", mod->export_cache.number_of_names == 0);
    check("reset_export_cache zeroes func count", mod->export_cache.number_of_functions == 0);

    reset_export_cache(NULL);
    check("reset_export_cache(NULL) does not crash", 1);

    munmap(base, buf_size);
}

/* ── Main ────────────────────────────────────────────────────── */

int main(void)
{
    printf("=== Export Parsing Tests ===\n");

    test_parse_export_table();
    test_parse_no_export();
    test_parse_malformed_export_bounds();
    test_lookup_export();
    test_lookup_malformed_export_name();
    test_lookup_export_by_ordinal();
    test_forwarder_detection();
    test_reset_export_cache();

    printf("\n========================================\n");
    printf("Total:  %d  Passed: %d  Failed: %d\n",
           total_tests, passed_tests, failed_tests);
    printf("========================================\n");

    return failed_tests > 0 ? 1 : 0;
}
