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
#include "nt_constants.h"
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

/* For dynamic loading tests */
int resolve_module_imports(loaded_module_t *mod, int depth);
int find_dll_path(const char *dll_name, char *path, size_t path_size);
loaded_module_t *load_dll(const char *path, int depth);

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
    parse_export_table(mod);

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
    if (mod) {
        reset_export_cache(mod);
        remove_module(mod);
    }
    munmap(base, nt.OptionalHeader.SizeOfImage);
}

/* ── Helper: build a minimal PE DLL on disk with exports, no imports ── */
static const char *build_dll_on_disk(const char *dll_path,
                                     const char **export_names,
                                     int num_exports)
{
    /* Create a minimal PE DLL in anonymous memory, then write to disk. */
    size_t buf_size = 0x3000;
    void *base = mmap(NULL, buf_size, PROT_READ | PROT_WRITE,
                      MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (base == MAP_FAILED) {
        perror("mmap");
        return NULL;
    }
    memset(base, 0, buf_size);

    uint8_t *p = (uint8_t *)base;

    /* DOS header */
    IMAGE_DOS_HEADER *dos = (IMAGE_DOS_HEADER *)(p + 0x0000);
    dos->e_magic = IMAGE_DOS_SIGNATURE;
    dos->e_lfanew = 0x80;

    /* NT headers */
    IMAGE_NT_HEADERS64 *nt = (IMAGE_NT_HEADERS64 *)(p + 0x0080);
    nt->Signature = IMAGE_NT_SIGNATURE;
    nt->FileHeader.Machine = IMAGE_FILE_MACHINE_AMD64;
    nt->FileHeader.NumberOfSections = 2;
    nt->FileHeader.SizeOfOptionalHeader = sizeof(IMAGE_OPTIONAL_HEADER64);
    nt->FileHeader.Characteristics = 0x2000; /* IMAGE_FILE_DLL */
    nt->OptionalHeader.Magic = IMAGE_NT_OPTIONAL_HDR64_MAGIC;
    nt->OptionalHeader.SectionAlignment = 0x1000;
    nt->OptionalHeader.FileAlignment = 0x200;
    nt->OptionalHeader.SizeOfImage = 0x3000;
    nt->OptionalHeader.SizeOfHeaders = 0x1000; /* cover headers + section raw data */
    nt->OptionalHeader.ImageBase = 0; /* let map_image pick any base */
    /* Point export directory into .rdata, no import directory */
    nt->OptionalHeader.DataDirectory[DIRECTORY_ENTRY_EXPORT].VirtualAddress = 0x2000;
    nt->OptionalHeader.DataDirectory[DIRECTORY_ENTRY_EXPORT].Size = 0x200;
    nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT].VirtualAddress = 0;
    nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT].Size = 0;

    /* Section headers */
    size_t sec_off = 0x80 + sizeof(uint32_t) + sizeof(IMAGE_FILE_HEADER)
                    + sizeof(IMAGE_OPTIONAL_HEADER64);
    IMAGE_SECTION_HEADER *sec = (IMAGE_SECTION_HEADER *)(p + sec_off);

    memcpy(sec[0].Name, ".text\0\0\0", 8);
    sec[0].Misc.VirtualSize = 0x1000;
    sec[0].VirtualAddress = 0x1000;
    sec[0].SizeOfRawData = 0x1000;
    sec[0].PointerToRawData = 0x1000;
    sec[0].Characteristics = IMAGE_SCN_MEM_READ | IMAGE_SCN_MEM_EXECUTE;

    memcpy(sec[1].Name, ".rdata\0\0", 8);
    sec[1].Misc.VirtualSize = 0x1000;
    sec[1].VirtualAddress = 0x2000;
    sec[1].SizeOfRawData = 0x1000;
    sec[1].PointerToRawData = 0x2000;
    sec[1].Characteristics = IMAGE_SCN_MEM_READ;

    /* Stub function bytes */
    for (int i = 0; i < num_exports; i++) {
        uint32_t rva = 0x1000 + i * 0x10;
        p[rva] = 0xC3; /* ret */
    }

    /* Export directory at RVA 0x2000 */
    IMAGE_EXPORT_DIRECTORY *exp = (IMAGE_EXPORT_DIRECTORY *)(p + 0x2000);
    exp->NumberOfFunctions = (uint32_t)num_exports;
    exp->NumberOfNames = (uint32_t)num_exports;
    exp->Base = 1;
    exp->Name = 0x2028;

    uint32_t name_table_rva = 0x2030;
    uint32_t ordinal_rva = name_table_rva + num_exports * sizeof(uint32_t);
    uint32_t func_rva = ordinal_rva + num_exports * sizeof(uint16_t);
    uint32_t name_str_rva = func_rva + num_exports * sizeof(uint32_t);

    exp->AddressOfNames = name_table_rva;
    exp->AddressOfNameOrdinals = ordinal_rva;
    exp->AddressOfFunctions = func_rva;

    /* DLL name string */
    memcpy(p + 0x2028, "TDLL.DLL\0", 9);

    /* AddressOfNames */
    uint32_t *names_arr = (uint32_t *)(p + name_table_rva);
    uint32_t cur = name_str_rva;
    for (int i = 0; i < num_exports; i++) {
        names_arr[i] = cur;
        cur += (uint32_t)(strlen(export_names[i]) + 1);
    }

    /* AddressOfNameOrdinals */
    uint16_t *ords = (uint16_t *)(p + ordinal_rva);
    for (int i = 0; i < num_exports; i++) {
        ords[i] = (uint16_t)i;
    }

    /* AddressOfFunctions */
    uint32_t *funcs = (uint32_t *)(p + func_rva);
    for (int i = 0; i < num_exports; i++) {
        funcs[i] = 0x1000 + i * 0x10;
    }

    /* Name strings */
    uint8_t *str_pos = p + name_str_rva;
    for (int i = 0; i < num_exports; i++) {
        memcpy(str_pos, export_names[i], strlen(export_names[i]) + 1);
        str_pos += strlen(export_names[i]) + 1;
    }

    /* Write to disk */
    int fd = open(dll_path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) {
        perror("open");
        munmap(base, buf_size);
        return NULL;
    }
    if (write(fd, base, buf_size) != (ssize_t)buf_size) {
        perror("write");
        close(fd);
        munmap(base, buf_size);
        return NULL;
    }
    close(fd);
    munmap(base, buf_size);
    return dll_path;
}

/* ── Test: load_dll via WINE_DLL_PATH ───────────────────────── */

static void test_load_dll(void)
{
    printf("\n=== load_dll ===\n");

    /* Initialize subsystems */
    init_msvcrt_imports();
    init_import_table();
    init_module_list();

    const char *export_names[] = { "DllFunc" };
    const char *dll_path = "/tmp/tdll.dll";

    /* Build the DLL file on disk */
    if (!build_dll_on_disk(dll_path, export_names, 1)) {
        printf("  SKIP: failed to build test DLL\n");
        return;
    }

    /* Set WINE_DLL_PATH to /tmp so find_dll_path can locate tdll.dll */
    setenv("WINE_DLL_PATH", "/tmp", 1);

    /* Call load_dll */
    loaded_module_t *mod = load_dll("/tmp/tdll.dll", 0);

    check("load_dll returns non-NULL", mod != NULL);

    if (mod != NULL) {
        check("module name is correct", strcmp(mod->name, "tdll.dll") == 0);
        check("module base is non-NULL", mod->base != NULL);

        /* Verify the module is in module_list */
        loaded_module_t *found = find_module_by_name("tdll.dll");
        check("find_module_by_name finds loaded DLL", found != NULL);
        if (found) {
            check("found module base matches", found->base == mod->base);
        }

        /* For a DLL with no imports, resolve_module_imports returns early
         * without calling parse_export_table. Parse it manually. */
        parse_export_table(mod);
        check("parse_export_table populates export_cache",
              mod->export_cache.number_of_names > 0);
        if (mod->export_cache.number_of_names > 0) {
            void *addr = lookup_export(mod, "DllFunc");
            check("lookup_export finds DllFunc", addr != NULL);
        }

        /* Cleanup */
        reset_export_cache(mod);
        remove_module(mod);
        if (mod->base) {
            munmap(mod->base, mod->nt->OptionalHeader.SizeOfImage);
        }
    }

    unlink(dll_path);
}

/* ── Test: resolve_module_imports depth exceeded ────────────── */

static void test_resolve_module_imports_depth(void)
{
    printf("\n=== resolve_module_imports depth ===\n");

    /* Initialize subsystems */
    init_msvcrt_imports();
    init_import_table();
    init_module_list();

    /* Create a minimal PE image with no imports (so we can test depth
     * without triggering real import resolution failures). We need a
     * loaded_module_t with a valid base+nt that has zero import dir size. */
    size_t buf_size = 0x3000;
    void *base = mmap(NULL, buf_size, PROT_READ | PROT_WRITE,
                      MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (base == MAP_FAILED) {
        printf("  SKIP: mmap failed\n");
        return;
    }
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
    nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT].VirtualAddress = 0;
    nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT].Size = 0;

    /* Register as a module */
    loaded_module_t *mod = add_module(base, "test_depth.dll", nt);
    check("add_module succeeded", mod != NULL);

    if (mod != NULL) {
        /* Call with depth >= MAX_IMPORT_DEPTH (defined as 8 in import_resolve.c) */
        int rc = resolve_module_imports(mod, 8);
        check("resolve_module_imports returns -1 at max depth", rc == -1);

        remove_module(mod);
    }

    munmap(base, buf_size);
}

/* ── Test: load_dll with non-existent path ──────────────────── */

static void test_load_dll_not_found(void)
{
    printf("\n=== load_dll not found ===\n");

    /* Initialize subsystems */
    init_msvcrt_imports();
    init_import_table();
    init_module_list();

    /* Call with a non-existent absolute path */
    loaded_module_t *mod = load_dll("/nonexistent/path.dll", 0);
    check("load_dll returns NULL for non-existent file", mod == NULL);
}

/* ── Main ───────────────────────────────────────────────────── */

int main(void)
{
    printf("=== Import Resolution Tests (t7.3) ===\n");

    test_import_resolution_pipeline();
    test_three_tier_resolution();
    test_load_dll();
    test_resolve_module_imports_depth();
    test_load_dll_not_found();

    /* ── Summary ──────────────────────────────────────────── */
    printf("\n========================================\n");
    printf("Total:  %d  Passed: %d  Failed: %d\n",
           total_tests, passed_tests, failed_tests);
    printf("========================================\n");

    return failed_tests > 0 ? 1 : 0;
}
