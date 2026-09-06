/*
 * pe32_entry_resolve.c -- PE32 user-entry selection and CRT init bypass.
 *
 * This logic is PE32-specific and deliberately separate from the rest of the
 * loader bootstrap so it can be tested and refactored independently.
 */

#include <stdlib.h>
#include <string.h>

#include "include/common.h"
#include "include/crt.h"
#include "include/pe_parser.h"
#include "src/pe_priv.h"
#include "../syscall/syscalls_inline.h"
#include "pe32_doom95_compat.h"
#include "pe32_entry_resolve.h"

static pe32_entry_type_t g_entry_type = PE32_ENTRY_TYPE_MAIN;

enum {
    MINGW_INITIALIZED_FALLBACK_BSS_OFFSET = 0x40,

    X86_OP_MOV_RM32_IMM32        = 0xc7,
    X86_MODRM_MOV_ABS32_IMM32    = 0x05,
    X86_OP_JMP_REL32             = 0xe9,
    X86_OP_CALL_REL32            = 0xe8,

    X86_MOV_ABS32_IMM32_DISP_OFF = 2,
    X86_MOV_ABS32_IMM32_IMM_OFF  = 6,
    X86_MOV_ABS32_IMM32_LEN      = 10,

    X86_REL32_BRANCH_DISP_OFF    = 1,
    X86_REL32_BRANCH_LEN         = 5,

    WATCOM_ENTRY_STUB_LEN        = X86_MOV_ABS32_IMM32_LEN + X86_REL32_BRANCH_LEN,
};

static inline uint32_t pe32_le32(const uint8_t *p)
{
    return p[0] | (p[1] << 8) | (p[2] << 16) | (p[3] << 24);
}

static inline int32_t pe32_le32s(const uint8_t *p)
{
    return (int32_t)pe32_le32(p);
}

static int pe32_abs32_to_rva(void *image_base, const IMAGE_NT_HEADERS *nt,
                             uint32_t abs, uint32_t *out_rva)
{
    uint32_t base = (uint32_t)(uintptr_t)image_base;

    if (abs < base)
        return 0;

    *out_rva = abs - base;
    return pe_rva_range_is_valid(*out_rva, sizeof(uint32_t),
                                 pe_size_of_image(nt));
}

static uint32_t pe32_section_span(const IMAGE_SECTION_HEADER *sec)
{
    uint32_t size = sec->Misc.VirtualSize;

    if (size == 0 || sec->SizeOfRawData > size)
        size = sec->SizeOfRawData;
    return size;
}

static int pe32_rva_in_section(uint32_t rva, uint32_t len,
                               const IMAGE_SECTION_HEADER *sec)
{
    uint32_t start = sec->VirtualAddress;
    uint32_t size = pe32_section_span(sec);
    uint32_t end = start + size;

    if (size == 0 || end < start)
        return 0;
    if (rva < start || rva >= end)
        return 0;
    return len <= end - rva;
}

static int pe32_rva_in_section_with_flags(void *image_base,
                                          const IMAGE_NT_HEADERS *nt,
                                          uint32_t rva, uint32_t len,
                                          uint32_t required_flags)
{
    const IMAGE_SECTION_HEADER *sections = get_image_sections(image_base, nt);
    uint16_t num_sections = pe_section_count(nt);

    for (uint16_t i = 0; i < num_sections; i++) {
        if ((sections[i].Characteristics & required_flags) == required_flags &&
            pe32_rva_in_section(rva, len, &sections[i])) {
            return 1;
        }
    }

    return 0;
}

static int pe32_extract_watcom_entry_from_entry_stub(void *image_base,
                                                     IMAGE_NT_HEADERS *nt,
                                                     uint32_t *out_rva)
{
    const crt_module_t *mod = crt_get_active();
    uint32_t entry_rva;
    const uint8_t *p;
    uint8_t branch_op;
    uint32_t store_rva;
    uint32_t store_abs;
    const IMAGE_SECTION_HEADER *sections;
    const IMAGE_SECTION_HEADER *code;
    uint32_t candidate_rva;
    uint32_t candidate_abs;
    int32_t branch_rel;
    uint32_t branch_next_rva;
    int64_t branch_target;

    if (mod == NULL || crt_module_type(mod) != CRT_TYPE_WATCOM)
        return 0;

    entry_rva = pe_entry_rva(nt);
    p = pe_rva_to_const_ptr(image_base, nt, entry_rva, WATCOM_ENTRY_STUB_LEN);
    if (p == NULL)
        return 0;

    if (p[0] != X86_OP_MOV_RM32_IMM32 ||
        p[1] != X86_MODRM_MOV_ABS32_IMM32)
        return 0;

    branch_op = p[X86_MOV_ABS32_IMM32_LEN];
    if (branch_op != X86_OP_JMP_REL32 && branch_op != X86_OP_CALL_REL32)
        return 0;

    store_abs = pe32_le32(p + X86_MOV_ABS32_IMM32_DISP_OFF);
    if (!pe32_abs32_to_rva(image_base, nt, store_abs, &store_rva) ||
        !pe32_rva_in_section_with_flags(image_base, nt, store_rva,
                                        sizeof(uint32_t),
                                        IMAGE_SCN_MEM_WRITE)) {
        return 0;
    }

    sections = get_image_sections(image_base, nt);
    code = find_code_section(nt, sections);
    if (code == NULL)
        return 0;

    candidate_abs = pe32_le32(p + X86_MOV_ABS32_IMM32_IMM_OFF);
    if (!pe32_abs32_to_rva(image_base, nt, candidate_abs, &candidate_rva) ||
        !pe32_rva_in_section(candidate_rva, 1, code)) {
        return 0;
    }

    branch_rel = pe32_le32s(p + X86_MOV_ABS32_IMM32_LEN +
                            X86_REL32_BRANCH_DISP_OFF);
    branch_next_rva = entry_rva + WATCOM_ENTRY_STUB_LEN;
    branch_target = (int64_t)branch_next_rva + (int64_t)branch_rel;
    if (branch_next_rva < entry_rva || branch_target < 0 ||
        branch_target > UINT32_MAX ||
        !pe_rva_range_is_valid((uint32_t)branch_target, 1,
                               pe_size_of_image(nt)) ||
        !pe32_rva_in_section((uint32_t)branch_target, 1, code)) {
        return 0;
    }

    *out_rva = candidate_rva;
    return 1;
}

uint32_t pe32_resolve_entry_symbol(void *image_base, IMAGE_NT_HEADERS *nt,
                                   const char *path)
{
    uint32_t pe_entry = pe_entry_rva(nt);
    uint32_t entry_rva = pe_entry;
    uint32_t ptr_sym = pe_pointer_to_symbol_table(nt);
    uint32_t num_sym = pe_number_of_symbols(nt);
    int had_symbols = 0;
    const IMAGE_SECTION_HEADER *sections = get_image_sections(image_base, nt);
    int num_sections = pe_section_count(nt);
    const crt_module_t *mod = crt_get_active();
    const char *const *entry_syms = crt_entry_symbols(mod);

    g_entry_type = PE32_ENTRY_TYPE_MAIN;

    if (pe32_is_doom95_path(path))
        return pe_entry;

    if (ptr_sym != 0 && num_sym != 0) {
        IMAGE_SYMBOL *symbols = NULL;
        char *string_table = NULL;
        int sym_count = parse_symbol_table_from_file(path, nt, &symbols,
                                                     &string_table);
        if (sym_count > 0) {
            uint32_t main_rva = 0;

            had_symbols = 1;
            if (entry_syms) {
                int si;

                for (si = 0; entry_syms[si] != NULL; si++) {
                    main_rva = lookup_symbol_rva(symbols, sym_count,
                                                 string_table, sections,
                                                 num_sections, entry_syms[si]);
                    if (main_rva != 0)
                        break;
                }

                if (main_rva != 0 && entry_syms[si] != NULL) {
                    const char *sym = entry_syms[si];
                    const char *p = sym;

                    if (*p == '_')
                        p++;
                    if ((strcmp(p, "WinMain@16") == 0) ||
                        (strcmp(p, "WinMain") == 0) ||
                        (strcmp(p, "Main@16") == 0)) {
                        g_entry_type = PE32_ENTRY_TYPE_WINMAIN;
                    } else if ((strcmp(p, "wWinMain@16") == 0) ||
                               (strcmp(p, "wWinMain") == 0) ||
                               (strcmp(p, "wMain@16") == 0)) {
                        g_entry_type = PE32_ENTRY_TYPE_WWINMAIN;
                    }
                }
            }

            if (main_rva == 0) {
                main_rva = lookup_symbol_rva(symbols, sym_count, string_table,
                                             sections, num_sections, "_main");
            }
            if (main_rva == 0) {
                main_rva = lookup_symbol_rva(symbols, sym_count, string_table,
                                             sections, num_sections, "main");
            }

            if (main_rva != 0)
                entry_rva = main_rva;

            free(symbols);
        }
    }

    if (entry_rva == pe_entry) {
        uint32_t extracted_rva;

        if (pe32_extract_watcom_entry_from_entry_stub(image_base, nt,
                                                      &extracted_rva)) {
            DEBUG_LEVEL(1, "watcom_entry_extract: user_func VA=0x%x -> RVA=0x%x%s",
                        (uint32_t)(uintptr_t)image_base + extracted_rva,
                        extracted_rva,
                        had_symbols ? " (fallback after symbol lookup miss)" : "");
            entry_rva = extracted_rva;
        }
    }

    return entry_rva;
}

void pe32_patch_crt_initialized(void *image_base, IMAGE_NT_HEADERS *nt,
                                const char *path)
{
    const crt_module_t *mod = crt_get_active();
    crt_type_t type = mod ? crt_module_type(mod) : CRT_TYPE_UNKNOWN;
    uint32_t ptr_sym;
    uint32_t num_sym;
    const IMAGE_SECTION_HEADER *sections;
    int num_sections;
    IMAGE_SYMBOL *symbols = NULL;
    char *string_table = NULL;
    int sym_count;
    uint32_t init_rva;
    uint8_t *addr;
    uintptr_t page;
    long rc;

    (void)path;

    if (type == CRT_TYPE_WATCOM || type == CRT_TYPE_UNKNOWN)
        return;

    ptr_sym = pe_pointer_to_symbol_table(nt);
    num_sym = pe_number_of_symbols(nt);
    if (ptr_sym == 0 || num_sym == 0)
        return;

    sections = get_image_sections(image_base, nt);
    num_sections = pe_section_count(nt);

    sym_count = parse_symbol_table_from_file(path, nt, &symbols, &string_table);
    if (sym_count <= 0)
        return;

    init_rva = lookup_symbol_rva(symbols, sym_count, string_table, sections,
                                 num_sections, "_initialized");
    if (init_rva == 0) {
        const IMAGE_SECTION_HEADER *bss = find_section_by_name(nt, sections, ".bss");

        if (bss)
            init_rva = bss->VirtualAddress + MINGW_INITIALIZED_FALLBACK_BSS_OFFSET;
    }
    free(symbols);

    if (init_rva == 0)
        return;

    addr = pe_rva_to_ptr(image_base, nt, init_rva, sizeof(uint32_t));
    if (addr == NULL)
        return;

    page = (uintptr_t)addr & ~(uintptr_t)PAGE_MASK;
    rc = INLINE_SYSCALL_MPROTECT((void *)page, PAGE_SIZE, PROT_READ | PROT_WRITE);
    if (rc == 0)
        *(uint32_t *)addr = 1;
}

pe32_entry_type_t pe32_get_entry_type(void)
{
    return g_entry_type;
}
