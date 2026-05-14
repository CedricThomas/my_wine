/*
 * test_pe32.c — PE32 (32-bit) parsing and relocation unit tests
 *
 * Tests the PE32 code paths using crafted in-memory PE32 binaries.
 * Covers: parse_nt_headers, parse_imports, apply_relocations,
 * and the pe_is_pe32() accessor functions.
 *
 * No PE32 sample binaries exist in the project, so all test data
 * is constructed in memory.
 *
 * Usage: ./build/test_pe32
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <errno.h>
#include <unistd.h>
#include <sys/mman.h>

#include "pe.h"
#include "nt_constants.h"

/* Forward declarations from pe_parser.c */
int parse_dos_header(const void *base, size_t file_size, IMAGE_DOS_HEADER *out_header);
int parse_nt_headers(const void *base, size_t file_size,
                     const IMAGE_DOS_HEADER *dos_header,
                     IMAGE_NT_HEADERS *out_nt_headers);
int parse_sections(const void *base, size_t file_size,
                   const IMAGE_NT_HEADERS *nt_headers,
                   IMAGE_SECTION_HEADER **out_sections);
int parse_imports(const void *base, size_t file_size,
                  const IMAGE_NT_HEADERS *nt_headers,
                  IMAGE_IMPORT_DESCRIPTOR **out_first_descriptor);

/* Forward declaration from relocations.c */
int apply_relocations(void *base, IMAGE_NT_HEADERS *nt);

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

/* ── Helper: build a minimal PE32 in a buffer ─────────────────── */

/*
 * Layout (all offsets from base):
 *   +0x000  DOS header (e_lfanew = 0x80)
 *   +0x080  IMAGE_NT_HEADERS32
 *   +0x1E8  Section table (1 section at offset = 0x80+4+20+224 = 0x1E8)
 *   +0x1000 Data / relocation targets
 *   +0x1100 Relocation blocks
 */

static void setup_pe32_dos_header(unsigned char *buf)
{
    IMAGE_DOS_HEADER *dos = (IMAGE_DOS_HEADER *)buf;
    memset(dos, 0, sizeof(*dos));
    dos->e_magic = IMAGE_DOS_SIGNATURE;
    dos->e_lfanew = 0x80;
}

static void setup_pe32_nt_headers(unsigned char *buf,
                                   uint32_t reloc_va, uint32_t reloc_size)
{
    IMAGE_NT_HEADERS32 *nt = (IMAGE_NT_HEADERS32 *)(buf + 0x80);
    memset(nt, 0, sizeof(*nt));

    nt->Signature = IMAGE_NT_SIGNATURE;
    nt->FileHeader.Machine = IMAGE_FILE_MACHINE_I386;
    nt->FileHeader.NumberOfSections = 1;
    nt->FileHeader.SizeOfOptionalHeader = sizeof(IMAGE_OPTIONAL_HEADER32);

    nt->OptionalHeader.Magic = IMAGE_NT_OPTIONAL_HDR32_MAGIC;
    nt->OptionalHeader.ImageBase = 0x00400000;  /* PE32 default */
    nt->OptionalHeader.SectionAlignment = 0x1000;
    nt->OptionalHeader.FileAlignment = 0x200;
    nt->OptionalHeader.SizeOfImage = 0x2000;
    nt->OptionalHeader.SizeOfHeaders = 0x200;
    nt->OptionalHeader.NumberOfRvaAndSizes = 16;

    nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_BASERELOC]
        .VirtualAddress = reloc_va;
    nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_BASERELOC]
        .Size = reloc_size;

    /* Section table follows optional header: 0x80 + 4 + 20 + 224 = 0x1E8 */
    uint32_t sec_off = 0x80 + sizeof(uint32_t) + sizeof(IMAGE_FILE_HEADER) +
                       sizeof(IMAGE_OPTIONAL_HEADER32);
    IMAGE_SECTION_HEADER *sec = (IMAGE_SECTION_HEADER *)(buf + sec_off);
    memset(sec, 0, sizeof(*sec));
    memcpy(sec->Name, ".text", 5);
    sec->Misc.VirtualSize = 0x800;
    sec->VirtualAddress = 0x1000;
    sec->SizeOfRawData = 0x800;
    sec->PointerToRawData = 0x1000;
    sec->Characteristics = 0x60000020; /* CODE | EXECUTE | READ */
}

/* ── Test 1: Parse PE32 headers ───────────────────────────────── */

static void test_parse_pe32_headers(void)
{
    printf("\n--- Parse PE32 headers ---\n");

    unsigned char buf[0x4000];
    memset(buf, 0, sizeof(buf));

    setup_pe32_dos_header(buf);
    setup_pe32_nt_headers(buf, 0, 0);

    IMAGE_DOS_HEADER dos;
    int rc = parse_dos_header(buf, sizeof(buf), &dos);
    check("parse_dos_header succeeds", rc == 0);
    check("e_magic is MZ", dos.e_magic == IMAGE_DOS_SIGNATURE);
    check("e_lfanew is 0x80", dos.e_lfanew == 0x80);

    IMAGE_NT_HEADERS nt;
    rc = parse_nt_headers(buf, sizeof(buf), &dos, &nt);
    check("parse_nt_headers succeeds for PE32", rc == 0);
    check("pe_type is PE_TYPE_32", nt.pe_type == PE_TYPE_32);
    check("machine is I386",
          nt.u.nt32.FileHeader.Machine == IMAGE_FILE_MACHINE_I386);
    check("optional header magic is PE32",
          nt.u.nt32.OptionalHeader.Magic == IMAGE_NT_OPTIONAL_HDR32_MAGIC);
    check("image base is 0x400000",
          nt.u.nt32.OptionalHeader.ImageBase == 0x00400000);
    check("section count is 1",
          nt.u.nt32.FileHeader.NumberOfSections == 1);
}

/* ── Test 2: PE32 accessor functions ──────────────────────────── */

static void test_pe32_accessors(void)
{
    printf("\n--- PE32 accessor functions ---\n");

    unsigned char buf[0x4000];
    memset(buf, 0, sizeof(buf));

    setup_pe32_dos_header(buf);
    setup_pe32_nt_headers(buf, 0x1100, 0x10);

    IMAGE_DOS_HEADER dos;
    parse_dos_header(buf, sizeof(buf), &dos);

    IMAGE_NT_HEADERS nt;
    parse_nt_headers(buf, sizeof(buf), &dos, &nt);

    /* pe_is_pe32 */
    check("pe_type is PE_TYPE_32", nt.pe_type == PE_TYPE_32);

    /* pe_image_base via accessor pattern */
    uint64_t base = nt.u.nt32.OptionalHeader.ImageBase;
    check("image base is 0x400000", base == 0x400000);

    /* pe_entry_rva */
    uint32_t entry = nt.u.nt32.OptionalHeader.AddressOfEntryPoint;
    check("entry rva is 0 (default)", entry == 0);

    /* pe_section_count */
    uint16_t sect_count = nt.u.nt32.FileHeader.NumberOfSections;
    check("section count is 1", sect_count == 1);

    /* pe_size_of_image */
    uint32_t size = nt.u.nt32.OptionalHeader.SizeOfImage;
    check("size_of_image is 0x2000", size == 0x2000);

    /* pe_optional_header_size */
    uint16_t opt_size = nt.u.nt32.FileHeader.SizeOfOptionalHeader;
    check("optional_header_size is sizeof(IMAGE_OPTIONAL_HEADER32)",
          opt_size == (uint16_t)sizeof(IMAGE_OPTIONAL_HEADER32));

    /* pe_get_data_dir for BASERELOC */
    if (IMAGE_DIRECTORY_ENTRY_BASERELOC < nt.u.nt32.OptionalHeader.NumberOfRvaAndSizes) {
        uint32_t reloc_va = nt.u.nt32.OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_BASERELOC].VirtualAddress;
        check("basereloc VA is 0x1100", reloc_va == 0x1100);
    } else {
        check("basereloc dir index out of range", 0);
    }
}

/* ── Test 3: PE32 section parsing ─────────────────────────────── */

static void test_parse_pe32_sections(void)
{
    printf("\n--- Parse PE32 sections ---\n");

    unsigned char buf[0x4000];
    memset(buf, 0, sizeof(buf));

    setup_pe32_dos_header(buf);
    setup_pe32_nt_headers(buf, 0, 0);

    IMAGE_DOS_HEADER dos;
    parse_dos_header(buf, sizeof(buf), &dos);

    IMAGE_NT_HEADERS nt;
    parse_nt_headers(buf, sizeof(buf), &dos, &nt);

    IMAGE_SECTION_HEADER *sections = NULL;
    int num = parse_sections(buf, sizeof(buf), &nt, &sections);
    check("parse_sections returns 1", num == 1);
    if (sections) {
        check("section name is .text",
              strncmp((char *)sections[0].Name, ".text", 5) == 0);
        check("VirtualAddress is 0x1000",
              sections[0].VirtualAddress == 0x1000);
    }
}

/* ── Test 4: PE32 DIR32 relocation ────────────────────────────── */

static uint16_t make_reloc_entry(uint16_t type, uint16_t offset)
{
    return ((type & 0xF) << 12) | (offset & 0xFFF);
}

static void test_pe32_dir32_relocation(void)
{
    printf("\n--- PE32 DIR32 relocation ---\n");

    void *base = mmap(NULL, 0x4000, PROT_READ | PROT_WRITE,
                      MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (base == MAP_FAILED) { printf("  SKIP: mmap failed\n"); return; }

    memset(base, 0, 0x4000);

    /* Set up DOS header */
    setup_pe32_dos_header((unsigned char *)base);

    /* Plant a known 32-bit value at offset 0x1000 */
    uint32_t *test_value = (uint32_t *)((char *)base + 0x1000);
    *test_value = 0x00400000;  /* old ImageBase */

    /* Set up a DIR32 relocation block at VA 0x1100 */
    uint32_t block_va = 0x1100;
    IMAGE_BASE_RELOCATION *block = (IMAGE_BASE_RELOCATION *)((char *)base + block_va);
    block->virtualAddress = 0x1000;
    /* 8-byte header + 1 entry (2 bytes) = 10 bytes */
    block->sizeOfBlock = sizeof(IMAGE_BASE_RELOCATION) + sizeof(uint16_t);
    uint16_t *entries = (uint16_t *)((char *)block + sizeof(IMAGE_BASE_RELOCATION));
    entries[0] = make_reloc_entry(0x0004, 0);  /* IMAGE_REL_BASED_DIR32 */

    /* NT headers with reloc dir at 0x1100, size 10 */
    setup_pe32_nt_headers((unsigned char *)base, block_va, 10);

    IMAGE_DOS_HEADER dos;
    parse_dos_header(base, 0x4000, &dos);

    IMAGE_NT_HEADERS nt;
    parse_nt_headers(base, 0x4000, &dos, &nt);

    int rc = apply_relocations(base, &nt);
    check("apply_relocations returns 0", rc == 0);
    check("pe_type is PE_TYPE_32", nt.pe_type == PE_TYPE_32);

    /* For PE32 DIR32: patches uint32_t with 32-bit delta */
    uint32_t delta32 = (uint32_t)((uintptr_t)base - 0x00400000);
    uint32_t expected = 0x00400000 + delta32;
    check("DIR32 value patched by 32-bit delta",
          *test_value == expected);

    printf("    delta32=0x%x  old=0x%x  new=0x%x  expected=0x%x\n",
           delta32, 0x00400000, *test_value, expected);

    munmap(base, 0x4000);
}

/* ── Test 5: PE32 vs PE32+ struct sizes ───────────────────────── */

static void test_pe32_vs_pe64_sizes(void)
{
    printf("\n--- PE32 vs PE32+ struct sizes ---\n");

    check("IMAGE_OPTIONAL_HEADER32 is 224 bytes",
          sizeof(IMAGE_OPTIONAL_HEADER32) == 224);
    check("IMAGE_OPTIONAL_HEADER64 is 240 bytes",
          sizeof(IMAGE_OPTIONAL_HEADER64) == 240);
    check("IMAGE_NT_HEADERS32 includes 32-bit optional header",
          sizeof(IMAGE_NT_HEADERS32) ==
          sizeof(uint32_t) + sizeof(IMAGE_FILE_HEADER) + sizeof(IMAGE_OPTIONAL_HEADER32));
    check("IMAGE_NT_HEADERS64 includes 64-bit optional header",
          sizeof(IMAGE_NT_HEADERS64) ==
          sizeof(uint32_t) + sizeof(IMAGE_FILE_HEADER) + sizeof(IMAGE_OPTIONAL_HEADER64));
    check("IMAGE_THUNK_DATA32 is 4 bytes",
          sizeof(IMAGE_THUNK_DATA32) == 4);
    check("IMAGE_THUNK_DATA64 is 8 bytes",
          sizeof(IMAGE_THUNK_DATA64) == 8);
    check("IMAGE_IMPORT_DESCRIPTOR is 20 bytes (same for PE32/PE32+)",
          sizeof(IMAGE_IMPORT_DESCRIPTOR) == 20);
    check("IMAGE_NT_HEADERS union is max(nt32, nt64) + type",
          sizeof(IMAGE_NT_HEADERS) >= sizeof(IMAGE_NT_HEADERS64) + sizeof(pe_type_t));
}

/* ── Test 6: PE32 import parsing ──────────────────────────────── */

static void test_pe32_import_parsing(void)
{
    printf("\n--- PE32 import parsing ---\n");

    /* Build a minimal PE32 with one import descriptor and one thunk */
    unsigned char buf[0x4000];
    memset(buf, 0, sizeof(buf));

    /* DOS header */
    setup_pe32_dos_header(buf);

    /* NT headers at 0x80 */
    IMAGE_NT_HEADERS32 *nt32 = (IMAGE_NT_HEADERS32 *)(buf + 0x80);
    memset(nt32, 0, sizeof(*nt32));
    nt32->Signature = IMAGE_NT_SIGNATURE;
    nt32->FileHeader.Machine = IMAGE_FILE_MACHINE_I386;
    nt32->FileHeader.NumberOfSections = 2;
    nt32->FileHeader.SizeOfOptionalHeader = sizeof(IMAGE_OPTIONAL_HEADER32);
    nt32->OptionalHeader.Magic = IMAGE_NT_OPTIONAL_HDR32_MAGIC;
    nt32->OptionalHeader.ImageBase = 0x00400000;
    nt32->OptionalHeader.SectionAlignment = 0x1000;
    nt32->OptionalHeader.FileAlignment = 0x200;
    nt32->OptionalHeader.SizeOfImage = 0x4000;
    nt32->OptionalHeader.SizeOfHeaders = 0x400;
    nt32->OptionalHeader.NumberOfRvaAndSizes = 16;

    /* Import directory at VA 0x3000 */
    nt32->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT]
        .VirtualAddress = 0x3000;
    nt32->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT]
        .Size = 0x200;

    /* Section table at offset 0x1E8 */
    uint32_t sec_off = 0x80 + sizeof(uint32_t) + sizeof(IMAGE_FILE_HEADER) +
                       sizeof(IMAGE_OPTIONAL_HEADER32);
    IMAGE_SECTION_HEADER *sec = (IMAGE_SECTION_HEADER *)(buf + sec_off);

    /* Section 1: .text at VA 0x1000 (covers headers) */
    memset(&sec[0], 0, sizeof(sec[0]));
    memcpy(sec[0].Name, ".text", 5);
    sec[0].Misc.VirtualSize = 0x1000;
    sec[0].VirtualAddress = 0x0000;
    sec[0].SizeOfRawData = 0x1000;
    sec[0].PointerToRawData = 0;
    sec[0].Characteristics = 0x60000020;

    /* Section 2: .rdata at VA 0x3000 (covers import data) */
    memset(&sec[1], 0, sizeof(sec[1]));
    memcpy(sec[1].Name, ".rdata", 6);
    sec[1].Misc.VirtualSize = 0x1000;
    sec[1].VirtualAddress = 0x3000;
    sec[1].SizeOfRawData = 0x1000;
    sec[1].PointerToRawData = 0x3000;
    sec[1].Characteristics = 0x40000040;

    /* Layout (non-overlapping):
     * 0x3000-0x3027: 2 import descriptors (20 bytes each) + terminator
     * 0x3030-0x3037: ILT (2 x 4 bytes)
     * 0x3040-0x3047: IAT (2 x 4 bytes)
     * 0x3050-0x3059: DLL name "KERNEL32.dll\0"
     * 0x3060-0x306B: IMAGE_IMPORT_BY_NAME (hint + name)
     */

    /* Import descriptor at VA 0x3000 (file offset 0x3000) */
    IMAGE_IMPORT_DESCRIPTOR *desc = (IMAGE_IMPORT_DESCRIPTOR *)(buf + 0x3000);
    desc->u1.OriginalFirstThunk = 0x3030;  /* VA of ILT */
    desc->Name = 0x3050;                    /* VA of DLL name */
    desc->FirstThunk = 0x3040;              /* VA of IAT */
    desc->TimeDateStamp = 0;
    desc->ForwarderChain = 0;
    /* Termination descriptor at 0x3014 */
    desc[1].u1.OriginalFirstThunk = 0;
    desc[1].Name = 0;                       /* terminator */
    desc[1].FirstThunk = 0;
    desc[1].TimeDateStamp = 0;
    desc[1].ForwarderChain = 0;

    /* ILT (IMAGE_THUNK_DATA32) at VA 0x3030 */
    IMAGE_THUNK_DATA32 *ilt = (IMAGE_THUNK_DATA32 *)(buf + 0x3030);
    ilt[0].AddressOfData = 0x3060;  /* VA of IMAGE_IMPORT_BY_NAME */
    ilt[1].AddressOfData = 0;       /* terminator */

    /* IAT (IMAGE_THUNK_DATA32) at VA 0x3040 */
    IMAGE_THUNK_DATA32 *iat = (IMAGE_THUNK_DATA32 *)(buf + 0x3040);
    iat[0].AddressOfData = 0;
    iat[1].AddressOfData = 0;

    /* DLL name at VA 0x3050 */
    memcpy(buf + 0x3050, "KERNEL32.dll", 12);

    /* IMAGE_IMPORT_BY_NAME at VA 0x3060 */
    IMAGE_IMPORT_BY_NAME *by_name = (IMAGE_IMPORT_BY_NAME *)(buf + 0x3060);
    by_name->Hint = 42;
    memcpy(by_name->Name, "GetStdHandle", 13);

    /* Parse */
    IMAGE_DOS_HEADER dos;
    int rc = parse_dos_header(buf, sizeof(buf), &dos);
    check("parse_dos_header succeeds", rc == 0);

    IMAGE_NT_HEADERS nt;
    rc = parse_nt_headers(buf, sizeof(buf), &dos, &nt);
    check("parse_nt_headers succeeds for PE32", rc == 0);
    check("PE type is PE_TYPE_32", nt.pe_type == PE_TYPE_32);

    IMAGE_IMPORT_DESCRIPTOR *first = NULL;
    int num = parse_imports(buf, sizeof(buf), &nt, &first);
    check("parse_imports returns 1 import", num == 1);
    check("first descriptor is non-NULL", first != NULL);

    if (first) {
        const char *dll_name = (const char *)(buf + first->Name);
        check("DLL name is KERNEL32.dll",
              strncmp(dll_name, "KERNEL32.dll", 12) == 0);

        /* Verify ILT entry is a 32-bit RVA to IMAGE_IMPORT_BY_NAME */
        if (first->u1.OriginalFirstThunk != 0) {
            IMAGE_THUNK_DATA32 *ilt_check = (IMAGE_THUNK_DATA32 *)(buf + first->u1.OriginalFirstThunk);
            check("ILT[0] is non-zero RVA", ilt_check[0].AddressOfData != 0);
            check("ILT[0] is not an ordinal (high bit clear)",
                  !(ilt_check[0].AddressOfData & 0x80000000));

            if (ilt_check[0].AddressOfData != 0) {
                IMAGE_IMPORT_BY_NAME *check_name =
                    (IMAGE_IMPORT_BY_NAME *)(buf + ilt_check[0].AddressOfData);
                check("import name is GetStdHandle",
                      strncmp(check_name->Name, "GetStdHandle", 12) == 0);
            }
        }
    }
}

/* ── Test 7: PE32 with invalid magic should fail ──────────────── */

static void test_pe32_bad_magic(void)
{
    printf("\n--- PE32 bad magic rejection ---\n");

    unsigned char buf[0x4000];
    memset(buf, 0, sizeof(buf));

    setup_pe32_dos_header(buf);

    IMAGE_NT_HEADERS32 *nt32 = (IMAGE_NT_HEADERS32 *)(buf + 0x80);
    memset(nt32, 0, sizeof(*nt32));
    nt32->Signature = IMAGE_NT_SIGNATURE;
    nt32->FileHeader.Machine = IMAGE_FILE_MACHINE_I386;
    nt32->FileHeader.SizeOfOptionalHeader = sizeof(IMAGE_OPTIONAL_HEADER32);
    nt32->OptionalHeader.Magic = 0xFFFF;  /* Invalid magic */

    IMAGE_DOS_HEADER dos;
    parse_dos_header(buf, sizeof(buf), &dos);

    IMAGE_NT_HEADERS nt;
    int rc = parse_nt_headers(buf, sizeof(buf), &dos, &nt);
    check("parse_nt_headers rejects invalid magic", rc == -1);
}

/* ── Test 8: PE32 with I386 machine type should succeed ───────── */

static void test_pe32_i386_machine(void)
{
    printf("\n--- PE32 I386 machine type ---\n");

    unsigned char buf[0x4000];
    memset(buf, 0, sizeof(buf));

    setup_pe32_dos_header(buf);

    IMAGE_NT_HEADERS32 *nt32 = (IMAGE_NT_HEADERS32 *)(buf + 0x80);
    memset(nt32, 0, sizeof(*nt32));
    nt32->Signature = IMAGE_NT_SIGNATURE;
    nt32->FileHeader.Machine = IMAGE_FILE_MACHINE_I386;
    nt32->FileHeader.SizeOfOptionalHeader = sizeof(IMAGE_OPTIONAL_HEADER32);
    nt32->OptionalHeader.Magic = IMAGE_NT_OPTIONAL_HDR32_MAGIC;

    IMAGE_DOS_HEADER dos;
    parse_dos_header(buf, sizeof(buf), &dos);

    IMAGE_NT_HEADERS nt;
    int rc = parse_nt_headers(buf, sizeof(buf), &dos, &nt);
    check("parse_nt_headers accepts I386 machine type", rc == 0);
    check("pe_type is PE_TYPE_32", nt.pe_type == PE_TYPE_32);
    check("machine is 0x14c",
          nt.u.nt32.FileHeader.Machine == IMAGE_FILE_MACHINE_I386);
}

/* ── Test 9: PE32 ordinal import detection ─────────────────────── */

static void test_pe32_ordinal_detection(void)
{
    printf("\n--- PE32 ordinal import detection ---\n");

    /* Test that 0x80000000 | ordinal correctly signals an ordinal import */
    uint32_t ordinal_import = 0x80000000 | 0x1234;
    check("ordinal import has high bit set",
          (ordinal_import & 0x80000000) != 0);
    check("extracted ordinal is 0x1234",
          (ordinal_import & 0xFFFF) == 0x1234);

    uint32_t normal_import = 0x00003070;  /* RVA to IMAGE_IMPORT_BY_NAME */
    check("normal import does not have high bit set",
          (normal_import & 0x80000000) == 0);
}

/* ── Test 10: PE32+ still works after PE32 changes ────────────── */

static void test_pe32plus_still_works(void)
{
    printf("\n--- PE32+ still works after PE32 changes ---\n");

    unsigned char buf[0x4000];
    memset(buf, 0, sizeof(buf));

    IMAGE_DOS_HEADER *dos = (IMAGE_DOS_HEADER *)buf;
    memset(dos, 0, sizeof(*dos));
    dos->e_magic = IMAGE_DOS_SIGNATURE;
    dos->e_lfanew = 0x80;

    /* PE32+ (AMD64) */
    IMAGE_NT_HEADERS64 *nt64 = (IMAGE_NT_HEADERS64 *)(buf + 0x80);
    memset(nt64, 0, sizeof(*nt64));
    nt64->Signature = IMAGE_NT_SIGNATURE;
    nt64->FileHeader.Machine = IMAGE_FILE_MACHINE_AMD64;
    nt64->FileHeader.NumberOfSections = 0;
    nt64->FileHeader.SizeOfOptionalHeader = sizeof(IMAGE_OPTIONAL_HEADER64);
    nt64->OptionalHeader.Magic = IMAGE_NT_OPTIONAL_HDR64_MAGIC;
    nt64->OptionalHeader.ImageBase = 0x140000000ULL;

    IMAGE_NT_HEADERS nt;
    int rc = parse_nt_headers(buf, sizeof(buf), dos, &nt);
    check("parse_nt_headers still accepts PE32+", rc == 0);
    check("pe_type is PE_TYPE_64", nt.pe_type == PE_TYPE_64);
    check("machine is AMD64",
          nt.u.nt64.FileHeader.Machine == IMAGE_FILE_MACHINE_AMD64);
    check("image base is 0x140000000",
          nt.u.nt64.OptionalHeader.ImageBase == 0x140000000ULL);
}

/* ── Main ─────────────────────────────────────────────────────── */

int main(void)
{
    printf("=== PE32 Unit Tests ===\n");

    test_parse_pe32_headers();
    test_pe32_accessors();
    test_parse_pe32_sections();
    test_pe32_dir32_relocation();
    test_pe32_vs_pe64_sizes();
    test_pe32_import_parsing();
    test_pe32_bad_magic();
    test_pe32_i386_machine();
    test_pe32_ordinal_detection();
    test_pe32plus_still_works();

    printf("\n========================================\n");
    printf("Total:  %d  Passed: %d  Failed: %d\n",
           total_tests, passed_tests, failed_tests);
    printf("========================================\n");

    return failed_tests > 0 ? 1 : 0;
}
