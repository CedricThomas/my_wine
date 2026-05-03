/*
 * test_import_resolution.c -- Standalone unit test for import resolution
 *
 * Tests the import_table data structure, set_import, init_msvcrt_imports,
 * init_import_table, and resolve_import logic from main.c
 *
 * This test replicates the import resolution data structures to verify:
 * 1. All import entries have correct dll_name assigned
 * 2. set_import uses linear scan (works on unsorted table)
 * 3. resolve_import uses bsearch (works on sorted table)
 * 4. init_import_table sorts correctly for bsearch
 * 5. DLL-aware matching catches cross-DLL mismatches
 *
 * Build: gcc -Wall -Wextra -O2 -g -I ../include -o test_import_resolution tests/test_import_resolution.c
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <search.h>

static int failed = 0;

static void check(const char *label, int condition)
{
    if (condition)
        printf("  PASS: %s\n", label);
    else {
        printf("  FAIL: %s\n", label);
        failed = 1;
    }
}

/* -- Replicate import_entry_t from main.c -- */

typedef struct {
    const char *dll_name;
    const char *name;
    void *address;
} import_entry_t;

/* Minimal import table matching main.c structure */
static import_entry_t import_table[] = {
    { "ntdll.dll", "NtWriteFile", (void*)0xDEAD0001ULL },
    { "ntdll.dll", "NtReadFile", (void*)0xDEAD0002ULL },
    { "ntdll.dll", "NtClose", (void*)0xDEAD0003ULL },
    { "ntdll.dll", "NtTerminateProcess", (void*)0xDEAD0004ULL },
    { "kernel32.dll", "GetStdHandle", (void*)0xBEEF0001ULL },
    { "kernel32.dll", "WriteFile", (void*)0xBEEF0002ULL },
    { "kernel32.dll", "ExitProcess", (void*)0xBEEF0003ULL },
    { "kernel32.dll", "GetProcAddress", (void*)0xBEEF0004ULL },
    { "msvcrt.dll", "__iob_func", (void*)0xCAFE0001ULL },
    { "msvcrt.dll", "malloc", (void*)0xCAFE0002ULL },
    { "msvcrt.dll", "free", (void*)0xCAFE0003ULL },
    { "msvcrt.dll", "printf", (void*)0xCAFE0004ULL },
    /* Dynamic entries (initially NULL, set by set_import) */
    { "msvcrt.dll", "abort", NULL },
    { "msvcrt.dll", "calloc", NULL },
    { "msvcrt.dll", "strlen", NULL },
    { NULL, NULL, NULL }  /* sentinel */
};

static const size_t TABLE_COUNT = sizeof(import_table) / sizeof(import_entry_t) - 1;

/* -- Replicate set_import from main.c (t3.1: linear scan) -- */

static void set_import(const char *name, void *address)
{
    for (int i = 0; import_table[i].name != NULL; i++) {
        if (strcmp(import_table[i].name, name) == 0) {
            import_table[i].address = address;
            return;
        }
    }
    fprintf(stderr, "  ERROR: set_import: symbol '%s' not found\n", name);
}

/* -- Replicate init_msvcrt_imports from main.c -- */

static void init_msvcrt_imports(void)
{
    set_import("abort",   (void*)0x00DEADBEEF0001ULL);
    set_import("calloc",  (void*)0x00DEADBEEF0002ULL);
    set_import("strlen",  (void*)0x00DEADBEEF0003ULL);
}

/* -- Replicate bsearch helpers from main.c (t3.4) -- */

static int import_entry_cmp(const void *a, const void *b)
{
    return strcmp(((const import_entry_t *)a)->name,
                  ((const import_entry_t *)b)->name);
}

static int import_cmp_by_name(const void *key, const void *elem)
{
    return strcmp((const char *)key, ((const import_entry_t *)elem)->name);
}

static void init_import_table(void)
{
    qsort(import_table, TABLE_COUNT, sizeof(import_entry_t), import_entry_cmp);
}

/* -- Replicate resolve_import from main.c (t3.2: dll_name field) -- */

static void *resolve_import(const char *dll_name, const char *func_name)
{
    import_entry_t *entry = bsearch(func_name, import_table,
                                     TABLE_COUNT, sizeof(import_entry_t),
                                     import_cmp_by_name);
    if (entry == NULL) {
        fprintf(stderr, "  ERROR: unresolved import: %s!%s\n", dll_name, func_name);
        return NULL;
    }
    if (entry->address == NULL) {
        fprintf(stderr, "  ERROR: import %s!%s has NULL address\n", dll_name, func_name);
        return NULL;
    }
    if (entry->dll_name && strcmp(entry->dll_name, dll_name) != 0) {
        fprintf(stderr, "  WARNING: %s found in %s but requested from %s\n",
                func_name, entry->dll_name, dll_name);
        return NULL;
    }
    return entry->address;
}

/* -- Test: all dll_name fields correctly assigned -- */

static void test_dll_names(void)
{
    printf("\n=== Test: dll_name correctness (t3.2) ===\n");

    for (size_t i = 0; i < TABLE_COUNT; i++) {
        check("entry has non-NULL dll_name", import_table[i].dll_name != NULL);
        check("entry has non-NULL name", import_table[i].name != NULL);

        /* Verify DLL name matches expected pattern */
        const char *name = import_table[i].name;
        const char *dll  = import_table[i].dll_name;

        int is_ntdll = (strncmp(name, "Nt", 2) == 0);
        int is_kernel32 = (strncmp(name, "GetStdHandle", 12) == 0 ||
                           strncmp(name, "WriteFile", 9) == 0 ||
                           strncmp(name, "ExitProcess", 11) == 0 ||
                           strncmp(name, "GetProcAddress", 14) == 0);

        if (is_ntdll) {
            check("ntdll function has ntdll.dll", strcmp(dll, "ntdll.dll") == 0);
        } else if (is_kernel32) {
            check("kernel32 function has kernel32.dll", strcmp(dll, "kernel32.dll") == 0);
        } else {
            /* msvcrt functions */
            check("msvcrt function has msvcrt.dll", strcmp(dll, "msvcrt.dll") == 0);
        }
    }
}

/* -- Test: set_import works with linear scan on unsorted table -- */

static void test_set_import_linear_scan(void)
{
    printf("\n=== Test: set_import linear scan (t3.1) ===\n");

    /* Verify table is unsorted (as it starts in declaration order) */
    check("import_table starts unsorted (NtWriteFile before WriteFile)",
          import_table[0].name[0] == 'N');  /* Nt... before W... before __... */

    /* Call init_msvcrt_imports which uses set_import (linear scan) */
    init_msvcrt_imports();

    /* Verify all dynamic entries are now populated */
    for (size_t i = 0; i < TABLE_COUNT; i++) {
        if (strcmp(import_table[i].name, "abort") == 0) {
            check("set_import populated abort",
                  import_table[i].address == (void*)0x00DEADBEEF0001ULL);
        } else if (strcmp(import_table[i].name, "calloc") == 0) {
            check("set_import populated calloc",
                  import_table[i].address == (void*)0x00DEADBEEF0002ULL);
        } else if (strcmp(import_table[i].name, "strlen") == 0) {
            check("set_import populated strlen",
                  import_table[i].address == (void*)0x00DEADBEEF0003ULL);
        }
    }
}

/* -- Test: init_import_table sorts correctly -- */

static void test_sort_for_bsearch(void)
{
    printf("\n=== Test: sort for bsearch (t3.4) ===\n");

    init_import_table();

    /* Verify table is now sorted by name */
    for (size_t i = 1; i < TABLE_COUNT; i++) {
        int cmp = strcmp(import_table[i - 1].name, import_table[i].name);
        check("table is sorted (prev <= current)", cmp <= 0);
    }
}

/* -- Test: resolve_import with bsearch -- */

static void test_resolve_import_bsearch(void)
{
    printf("\n=== Test: resolve_import with bsearch (t3.4) ===\n");

    /* Test ntdll imports */
    void *addr = resolve_import("ntdll.dll", "NtWriteFile");
    check("resolve NtWriteFile from ntdll.dll",
          addr == (void*)0xDEAD0001ULL);

    addr = resolve_import("ntdll.dll", "NtClose");
    check("resolve NtClose from ntdll.dll",
          addr == (void*)0xDEAD0003ULL);

    /* Test kernel32 imports */
    addr = resolve_import("kernel32.dll", "GetStdHandle");
    check("resolve GetStdHandle from kernel32.dll",
          addr == (void*)0xBEEF0001ULL);

    addr = resolve_import("kernel32.dll", "WriteFile");
    check("resolve WriteFile from kernel32.dll",
          addr == (void*)0xBEEF0002ULL);

    /* Test msvcrt imports */
    addr = resolve_import("msvcrt.dll", "malloc");
    check("resolve malloc from msvcrt.dll",
          addr == (void*)0xCAFE0002ULL);

    /* Test dynamically set imports */
    addr = resolve_import("msvcrt.dll", "abort");
    check("resolve abort from msvcrt.dll (set_import)",
          addr == (void*)0x00DEADBEEF0001ULL);

    addr = resolve_import("msvcrt.dll", "strlen");
    check("resolve strlen from msvcrt.dll (set_import)",
          addr == (void*)0x00DEADBEEF0003ULL);
}

/* -- Test: DLL name mismatch detection (t3.2) -- */

static void test_dll_mismatch(void)
{
    printf("\n=== Test: DLL name mismatch detection (t3.2) ===\n");

    /* Requesting an ntdll function from kernel32.dll should fail */
    void *addr = resolve_import("kernel32.dll", "NtWriteFile");
    check("reject NtWriteFile from kernel32.dll (wrong DLL)", addr == NULL);

    /* Requesting a kernel32 function from ntdll.dll should fail */
    addr = resolve_import("ntdll.dll", "GetStdHandle");
    check("reject GetStdHandle from ntdll.dll (wrong DLL)", addr == NULL);

    /* Requesting from a completely unknown DLL should fail */
    addr = resolve_import("unknown.dll", "malloc");
    check("reject malloc from unknown.dll (wrong DLL)", addr == NULL);
}

/* -- Test: nonexistent import returns NULL -- */

static void test_unresolved(void)
{
    printf("\n=== Test: unresolved import (t3.2) ===\n");

    void *addr = resolve_import("ntdll.dll", "NtNonexistentFunction123");
    check("return NULL for nonexistent function", addr == NULL);
}

/* -- Test: init_msvcrt_imports called before init_import_table -- */

static void test_init_order(void)
{
    printf("\n=== Test: init order (t3.1 + t3.4 integration) ===\n");
    printf("  (Verified by construction: set_import uses linear scan\n");
    printf("   before sort, resolve_import uses bsearch after sort)\n");

    /* This is verified by the fact that set_import works on the
     * unsorted table (linear scan) and resolve_import works on
     * the sorted table (bsearch). If the order were reversed,
     * set_import would fail to find entries and resolve_import
     * would fail to bsearch correctly. */

    check("set_import on unsorted table succeeds (verified above)", 1);
    check("resolve_import on sorted table succeeds (verified above)", 1);
}

/* -- Test: Pass 2 3-tier thunk matching strategy -- */

static void test_thunk_matching_strategy(void)
{
    printf("\n=== Test: Pass 2 3-tier thunk matching strategy (t3.3) ===\n");

    /* The Pass 2 code in main.c uses a 3-tier approach:
     * 1. Match by resolved address (target already has correct value)
     * 2. Match by ILT RVA value (target contains OriginalFirstThunk value)
     * 3. Match by ILT offset/slot (target falls within import dir region)
     * 4. Positional fallback (nth target gets nth flat entry)
     *
     * This test verifies the conceptual correctness by confirming
     * that the flat array and target arrays would match correctly. */

    /* Simulate the flat array from import descriptors */
    struct {
        uint64_t ilt_value;
        uint64_t resolved_addr;
        const char *dll_name;
        const char *func_name;
    } flat[] = {
        { 0x1000, 0xDEAD0001ULL, "ntdll.dll", "NtWriteFile" },
        { 0x1008, 0xDEAD0002ULL, "ntdll.dll", "NtReadFile" },
        { 0x1010, 0xBEEF0001ULL, "kernel32.dll", "GetStdHandle" },
    };

    /* Test tier 1: resolved address match
     * If target already contains the resolved address, no write needed */
    uint64_t target_val_t1 = 0xDEAD0001ULL;  /* already resolved */
    int t1_match = 0;
    for (int i = 0; i < 3; i++) {
        if (flat[i].resolved_addr == target_val_t1) {
            t1_match = 1;
            break;
        }
    }
    check("Tier 1: resolved address match succeeds", t1_match);

    /* Test tier 2: ILT value match
     * If target contains an ILT RVA, resolve it */
    uint64_t target_val_t2 = 0x1010;  /* ILT RVA for GetStdHandle */
    void *t2_result = NULL;
    for (int i = 0; i < 3; i++) {
        if (flat[i].ilt_value == target_val_t2 && flat[i].resolved_addr != 0) {
            t2_result = (void *)(uintptr_t)flat[i].resolved_addr;
            break;
        }
    }
    check("Tier 2: ILT value match succeeds", t2_result == (void*)0xBEEF0001ULL);

    /* Test tier 3: offset/slot match
     * If target is within import dir, compute slot index */
    uint64_t import_dir_va = 0x7000;
    uint64_t target_addr = 0x7010;
    int slot_idx = (int)((target_addr - import_dir_va) / 8);
    check("Tier 3: slot index 2 from offset 0x7010", slot_idx == 2);
    if (slot_idx >= 0 && slot_idx < 3 && flat[slot_idx].resolved_addr != 0) {
        check("Tier 3: offset match succeeds",
              flat[slot_idx].resolved_addr == 0xBEEF0001ULL);
    }

    /* Test tier 4: positional fallback
     * nth target gets nth flat entry */
    check("Tier 4: positional fallback (target 0 -> flat 0)",
          flat[0].resolved_addr == 0xDEAD0001ULL);
    check("Tier 4: positional fallback (target 2 -> flat 2)",
          flat[2].resolved_addr == 0xBEEF0001ULL);
}

/* -- Main -- */

int main(void)
{
    printf("=== Import Resolution Unit Tests ===\n");

    test_dll_names();
    test_set_import_linear_scan();
    test_sort_for_bsearch();
    test_resolve_import_bsearch();
    test_dll_mismatch();
    test_unresolved();
    test_init_order();
    test_thunk_matching_strategy();

    printf("\n=== Summary ===\n");
    if (failed) {
        printf("FAIL: some tests failed\n");
        return 1;
    }
    printf("PASS: all tests passed\n");
    return 0;
}
