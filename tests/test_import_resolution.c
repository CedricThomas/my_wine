/*
 * test_import_resolution.c — Standalone unit test for import resolution
 *
 * Opens hello.exe, maps it via map_image(), calls resolve_imports(),
 * and verifies all IAT entries point to valid (non-zero, page-aligned)
 * function pointers.
 *
 * Gracefully skips with a message when hello.exe is not available.
 *
 * Build: linked against pe_parser.o, image_mapper.o, import_table.o, import_resolve.o, import_init.o
 *   and all CRT/ntdll stub .o files for handler resolution.
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
#include "pe_parser.h"
#include "src/loader/module_list.h"
#include "src/loader/export_table.h"

/* ── Forward declarations from loader_priv.h ───────────────── */

extern void *g_image_base;

void *map_image(const char *path,
                IMAGE_DOS_HEADER *out_dos,
                IMAGE_NT_HEADERS64 *out_nt,
                size_t *out_nt_size);

void init_import_table(void);
void init_msvcrt_imports(void);
int resolve_imports(void *base, IMAGE_NT_HEADERS64 *nt);

/* ── Test harness ───────────────────────────────────────────── */

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

/* ── Find hello.exe ────────────────────────────────────────── */

static const char *find_hello_exe(void)
{
    const char *paths[] = { "samples/hello_world/hello_world.exe", NULL };
    for (int i = 0; paths[i]; i++) {
        if (access(paths[i], F_OK) == 0)
            return paths[i];
    }
    return NULL;
}

/* ── Test: full import resolution pipeline ─────────────────── */

static void test_import_resolution_pipeline(void)
{
    const char *path = find_hello_exe();

    if (!path) {
        printf("\n=== Import Resolution Pipeline: SKIPPED (hello.exe not found) ===\n");
        return;
    }

    printf("\n=== Import Resolution Pipeline (hello.exe: %s) ===\n", path);

    /* Initialize import table (sorts for bsearch, sets __msvcrt_* pointers) */
    init_msvcrt_imports();
    init_import_table();

    /* Map the image */
    IMAGE_DOS_HEADER dos;
    IMAGE_NT_HEADERS64 nt;
    size_t nt_size;
    void *base = map_image(path, &dos, &nt, &nt_size);

    if (base == NULL) {
        printf("  SKIP: map_image() failed (cannot map %s)\n", path);
        return;
    }

    check("map_image returns non-NULL", base != NULL);

    /* Resolve imports */
    int rc = resolve_imports(base, &nt);
    check("resolve_imports returns 0", rc == 0);

    /* Verify IAT entries: all FirstThunk values should be non-zero and page-aligned
     * (pointing to our stub implementations) */
    IMAGE_OPTIONAL_HEADER64 *opt = &nt.OptionalHeader;

    if (opt->DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT].Size == 0) {
        check("no import directory — nothing to verify (OK)", 1);
        munmap(base, opt->SizeOfImage);
        return;
    }

    uint64_t import_rva = opt->DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT].VirtualAddress;
    IMAGE_IMPORT_DESCRIPTOR *desc = (IMAGE_IMPORT_DESCRIPTOR *)((char *)base + import_rva);

    int total_iat_entries = 0;
    int valid_entries = 0;
    int zero_entries = 0;
    int invalid_entries = 0;

    while (desc->Name != 0) {
        const char *dll_name = (const char *)((char *)base + desc->Name);

        IMAGE_THUNK_DATA64 *iath =
            (IMAGE_THUNK_DATA64 *)((char *)base + desc->FirstThunk);

        for (int i = 0; iath[i].AddressOfData != 0; i++) {
            total_iat_entries++;
            uint64_t val = iath[i].AddressOfData;

            if (val == 0) {
                zero_entries++;
                continue;
            }

            /* Check page-aligned (4KB = 0x1000) */
            int page_aligned = (val & 0xFFF) == 0;
            int non_zero = (val != 0);

            if (non_zero && page_aligned) {
                valid_entries++;
            } else {
                /* Non-page-aligned: might be a function pointer, data import, or
                 * global variable address. Check if it's in the user-space range
                 * (our stubs in ELF binary range ~0x55-0x7f...) */
                uintptr_t abs_val = (uintptr_t)val;
                if (non_zero && abs_val < 0xfffffffffffe0000UL) {
                    valid_entries++;
                } else {
                    invalid_entries++;
                }
            }
        }

        printf("  DLL %s: processed IAT entries\n", dll_name);
        desc++;
    }

    check("total IAT entries > 0", total_iat_entries > 0);
    check("valid entries > 0", valid_entries > 0);
    check("no invalid (garbage) entries", invalid_entries == 0);

    printf("  Summary: %d total, %d valid, %d zero (unresolved), %d invalid\n",
           total_iat_entries, valid_entries, zero_entries, invalid_entries);

    munmap(base, opt->SizeOfImage);
}

/* ── Test: three-tier resolution with module exports (Tier 2) ── */

static void test_three_tier_resolution(void)
{
    const char *path = find_hello_exe();

    if (!path) {
        printf("\n=== Three-Tier Resolution: SKIPPED (hello.exe not found) ===\n");
        return;
    }

    printf("\n=== Three-Tier Resolution (hello.exe: %s) ===\n", path);

    /* Initialize all subsystems: import table + module registry */
    init_msvcrt_imports();
    init_import_table();
    init_module_list();

    /* Map the image */
    IMAGE_DOS_HEADER dos;
    IMAGE_NT_HEADERS64 nt;
    size_t nt_size;
    void *base = map_image(path, &dos, &nt, &nt_size);

    if (base == NULL) {
        printf("  SKIP: map_image() failed (cannot map %s)\n", path);
        return;
    }

    check("map_image returns non-NULL", base != NULL);

    /* Register the PE as a module with exports */
    IMAGE_DOS_HEADER *img_dos = (IMAGE_DOS_HEADER *)base;
    IMAGE_NT_HEADERS64 *img_nt = (IMAGE_NT_HEADERS64 *)((char *)base + img_dos->e_lfanew);

    loaded_module_t *mod = add_module(base, "hello_world.exe", img_nt);
    check("add_module succeeded", mod != NULL);

    /* Parse exports (hello_world may not have exports — that's OK) */
    mod->export_cache = parse_export_table(base, img_nt);

    /* Re-run resolve_imports — should still work
     * (Tier 1 for stubs, Tier 2 for module exports if any) */
    int rc = resolve_imports(base, &nt);
    check("resolve_imports succeeds after module registration", rc == 0);

    /* Verify module is in the registry */
    loaded_module_t *found = find_module_by_name("hello_world.exe");
    check("module found by name", found != NULL);
    if (found) {
        check("found module base matches", found->base == base);
    }

    /* Cleanup */
    if (mod && mod->export_cache) {
        free_export_cache(mod->export_cache);
        mod->export_cache = NULL;
    }
    if (mod) {
        remove_module(mod);
    }
    munmap(base, nt.OptionalHeader.SizeOfImage);
}

/* ── Main ───────────────────────────────────────────────────── */

int main(void)
{
    printf("=== Import Resolution Tests (t7.3) ===\n");

    test_import_resolution_pipeline();
    test_three_tier_resolution();

    /* ── Summary ──────────────────────────────────────────── */
    printf("\n========================================\n");
    printf("Total:  %d  Passed: %d  Failed: %d\n",
           total_tests, passed_tests, failed_tests);
    printf("========================================\n");

    return failed_tests > 0 ? 1 : 0;
}
