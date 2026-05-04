/*
 * crt_refptrs.c — CRT refptr patching.
 *
 * Patches PE refptr entries so that CRT startup code doesn't crash
 * on two-level indirection through unmapped addresses.
 *
 * Two strategies:
 *   1. COFF symbol table (if available) — name-based discovery
 *   2. .text instruction scanning — find rip-relative loads followed
 *      by dereferences, then patch the target addresses
 */

#define _GNU_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#include "include/common.h"
#include "msvcrt_priv.h"

const refptr_mapping_t refptr_mappings[] = {
    { "__CTOR_LIST__",              (void *)&ctor_list_stub },
    { "__DTOR_LIST__",              (void *)&dtor_list_stub },
    { "__xi_a",                     (void *)&xi_a_stub },
    { "__dyn_tls_init_callback",    (void *)&dyn_tls_callback_stub },
    { "__image_base__",             (void *)&g_crt_ctx.image_base },
    { "__imp___initenv",            (void *)&__imp___initenv_stub },
    { "__mingw_oldexcpt_handler",   (void *)&mingw_excpt_handler_stub },
    { "__native_startup_lock",      (void *)&native_startup_lock },
    { "__native_startup_state",     (void *)&native_startup_state },
    { "__xc_a",                     (void *)&xc_a_stub },
    { "__xc_z",                     (void *)&xc_z_stub },
    { "__xi_a (dup)",               (void *)&xi_a_stub },
    { "__xi_z",                     (void *)&xi_z_stub },
    { "_commode",                   (void *)&_commode },
    { "__imp__acmdln",          (void *)&_acmdln },
    { "_dowildcard",                (void *)&dowildcard_val },
    { "_fmode",                     (void *)&_fmode },
    { "_newmode",                   (void *)&newmode_val },
    { "mingw_app_type",             (void *)&__msvcrt_app_type },
    { NULL, NULL }
};

static struct refptr_patch_arg {
    uint64_t *refptr;
    void *target;
    const char *name;
    uint64_t rva;
} refptr_patch_arg;

static void refptr_patch_cb(void *arg)
{
    struct refptr_patch_arg *a = (struct refptr_patch_arg *)arg;
    uint64_t old_val = (uint64_t)(uintptr_t)*a->refptr;
    *a->refptr = (uint64_t)(uintptr_t)a->target;
    fprintf(stderr, "patch_crt_refptrs: %s at rva 0x%lx: 0x%lx -> %p\n",
            a->name, (unsigned long)a->rva, old_val, a->target);
}

static void apply_refptr_patch(void *image_base, uint64_t rva, void *target,
                               const char *name, uint64_t image_size)
{
    if (rva >= image_size) {
        return;
    }

    uint64_t *refptr = (uint64_t *)((char *)image_base + rva);
    char *page_start = (char *)((uint64_t)(char *)refptr & ~(uint64_t)PAGE_MASK);

    refptr_patch_arg.refptr = refptr;
    refptr_patch_arg.target = target;
    refptr_patch_arg.name = name;
    refptr_patch_arg.rva = rva;

    if (with_mprotect_rw(page_start, PAGE_SIZE, refptr_patch_cb, &refptr_patch_arg, PROT_READ) != 0) {
        perror("patch_crt_refptrs: with_mprotect_rw");
    }
}

/*
 * Scan .text for the pattern:
 *   mov reg64, [rip + disp32]
 *   mov reg64, [reg64]
 *
 * This two-level indirection is the signature of a .refptr usage.
 * We collect all such target addresses and try to match them against
 * known CRT symbols by checking their current values.
 */
static void scan_text_for_refptrs(void *image_base, IMAGE_NT_HEADERS64 *nt,
                                  IMAGE_SECTION_HEADER *sections,
                                  uint64_t image_size)
{
    IMAGE_SECTION_HEADER *text_sec = find_section_by_name(nt, sections, ".text");
    if (!text_sec) {
        fprintf(stderr, "patch_crt_refptrs: no .text section\n");
        return;
    }

    uint64_t text_vaddr = text_sec->VirtualAddress;
    uint64_t text_size = text_sec->Misc.VirtualSize;
    if (text_size == 0) text_size = text_sec->SizeOfRawData;
    uint8_t *text_base = (uint8_t *)image_base + text_vaddr;

    int found = 0;
    for (uint64_t off = 0; off + 7 < text_size; off++) {
        uint8_t *p = text_base + off;

        /* 48 8B 05 disp32 = mov rax, [rip+disp32] */
        if (p[0] != X86_REX_W || p[1] != X86_MOV_R64_RIP || p[2] != X86_MOV_RIP) continue;

        int32_t disp = (int32_t)*((int32_t *)(p + 3));
        uint64_t instr_rva = text_vaddr + off;
        uint64_t target_rva = instr_rva + 7 + disp;

        if (target_rva >= image_size) continue;
        if (target_rva >= text_vaddr && target_rva < text_vaddr + text_size) continue;

        /* Look forward up to 40 bytes for deref+write pattern:
         *   48 8B 00 (mov rax, [rax])
         *   XX 89 00 or XX C7 00 (write to [rax])
         * This distinguishes __imp___initenv (deref+write) from callbacks (deref+test+call). */
        int has_write_deref = 0;
        for (uint64_t d = 7; d + 4 < 40 && d + 4 < text_size - off; d++) {
            if (p[d] == X86_REX_W && p[d+1] == X86_MOV_R64_RIP && p[d+2] == X86_MOV_RAX_RAX) {
                /* Found deref. Check if next is a store to [rax].
                 * Store patterns: 89 XX, C7 XX, 48 89 XX, 4C 89 XX where XX has mod=00,rm=00 */
                uint64_t nd = d + 3;
                if (nd + 2 < 40 && nd + 2 < text_size - off) {
                    /* Check for store to [rax] after deref.
                     * Patterns: 89 00, C7 00, or with REX prefix: 48/4C/4D/49 89 00 */
                    uint8_t a = p[nd], b = p[nd+1], c = p[nd+2];
                    int is_store = 0;
                    if (a == X86_MOV_RM_R64 && b == X86_MOV_RAX_RAX) is_store = 1;           /* 89 00 */
                    if (a == X86_MOV_RM_IMM && b == X86_MOV_RAX_RAX) is_store = 1;           /* C7 00 */
                    if ((a & 0xF0) == 0x40 && b == X86_MOV_RM_R64 && c == X86_MOV_RAX_RAX)   /* REX 89 00 */
                        is_store = 1;
                    if ((a & 0xF0) == 0x40 && b == X86_MOV_RM_IMM && c == X86_MOV_RAX_RAX)   /* REX C7 00 */
                        is_store = 1;
                    if (is_store) {
                        has_write_deref = 1;
                        break;
                    }
                }
                break;  /* No write after deref — not __imp___initenv */
            }
        }

        if (!has_write_deref) continue;

        /* Found refptr target at target_rva. Check if in a data section. */
        uint64_t *target_ptr = (uint64_t *)((char *)image_base + target_rva);
        uint64_t current = *target_ptr;
        int in_data_section = 0;
        const char *sec_name = "unknown";
        for (uint16_t si = 0; si < nt->FileHeader.NumberOfSections; si++) {
            IMAGE_SECTION_HEADER *sec = &sections[si];
            if (sec == text_sec) continue;
            uint64_t sec_end = sec->VirtualAddress +
                (sec->Misc.VirtualSize > 0 ? sec->Misc.VirtualSize : sec->SizeOfRawData);
            if (target_rva >= sec->VirtualAddress && target_rva < sec_end) {
                sec_name = (const char *)sec->Name;
                if ((target_rva - sec->VirtualAddress) % 8 == 0) {
                    in_data_section = 1;
                }
                break;
            }
        }
        fprintf(stderr, "patch_crt_refptrs: text-scan refptr at rva 0x%lx val=0x%lx in '%s' data=%d\n",
                (unsigned long)target_rva, current, sec_name, in_data_section);

        if (!in_data_section) continue;

        /* Patch refptr targets that point to PE-internal addresses
         * (they likely point to IAT entries or other host-addr data) */
        if (__imp___initenv_stub != NULL) {
            /* Patch to our stub — the CRT will deref to get .bss+0x18 */
            apply_refptr_patch(image_base, target_rva,
                               (void *)&__imp___initenv_stub,
                               "__imp___initenv (text-scan)", image_size);
            found = 1;
        }
    }

    if (!found)
        fprintf(stderr, "patch_crt_refptrs: text-scan found no refptr targets\n");
}

/*
 * Read the COFF symbol table from the PE file to find symbol addresses.
 * Returns the RVA (relative virtual address) of the symbol, or 0 if not found.
 */
static uint64_t find_symbol_rva_from_file(const char *file_path,
                                          IMAGE_NT_HEADERS64 *nt,
                                          IMAGE_SECTION_HEADER *sections,
                                          const char *name)
{
    uint32_t sym_ptr = nt->FileHeader.PointerToSymbolTable;
    uint32_t sym_count = nt->FileHeader.NumberOfSymbols;

    if (sym_ptr == 0 || sym_count == 0)
        return 0;

    int fd = open(file_path, O_RDONLY);
    if (fd < 0) return 0;

    struct stat st;
    if (fstat(fd, &st) < 0) { close(fd); return 0; }

    void *file_map = mmap(NULL, st.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
    close(fd);
    if (file_map == MAP_FAILED) return 0;

    IMAGE_SYMBOL *symbols = (IMAGE_SYMBOL *)((char *)file_map + sym_ptr);
    size_t sym_table_size = (size_t)sym_count * IMAGE_SIZEOF_SYMBOL;

    char *string_table = NULL;
    size_t str_off = sym_ptr + sym_table_size;
    if (str_off + 4 <= (size_t)st.st_size) {
        uint32_t str_size = *((const uint32_t *)((char *)file_map + str_off));
        if (str_off + 4 + str_size <= (size_t)st.st_size && str_size > 0) {
            string_table = (char *)file_map + str_off + 4;
        }
    }

    /*
     * Scan all symbols, preferring section-bound over absolute.
     * Some mingw-w64 builds have garbage absolute symbols (sec=0) with
     * wrong values that appear before the real section-bound entry.
     */
    uint64_t best_rva = 0;
    int has_section_match = 0;

    for (uint32_t i = 0; i < sym_count; i++) {
        const IMAGE_SYMBOL *sym = &symbols[i];
        const char *sym_name = get_symbol_name(sym, string_table);
        if (!sym_name) continue;
        size_t sym_name_len = strlen(sym_name);

        int matched = 0;
        /* Prefer .refptr entries over bare symbol names.
         * mingw-w64 COFF tables often have bare "mingw_app_type" in .idata
         * (wrong address) and ".rdata$.refptr.mingw_app_type" / ".refptr.mingw_app_type"
         * in .rdata (correct address). Check refptr prefixes FIRST so we
         * always find the right entry before the bare-name fallback. */
        const char *prefix = ".rdata$.refptr.";
        size_t plen = strlen(prefix);
        if (sym_name_len > plen &&
            strncmp(sym_name, prefix, plen) == 0 &&
            strncmp(sym_name + plen, name, sym_name_len - plen) == 0 &&
            name[sym_name_len - plen] == '\0') {
            matched = 1;
        }
        if (!matched) {
            const char *prefix2 = ".refptr.";
            size_t plen2 = strlen(prefix2);
            if (sym_name_len > plen2 &&
                strncmp(sym_name, prefix2, plen2) == 0 &&
                strncmp(sym_name + plen2, name, sym_name_len - plen2) == 0 &&
                name[sym_name_len - plen2] == '\0') {
                matched = 1;
            }
        }
        if (!matched) {
            /* Exact name match (last resort — .idata may have bare name with wrong address) */
            if (strncmp(sym_name, name, sym_name_len) == 0 &&
                name[sym_name_len] == '\0') {
                matched = 1;
            }
        }
        if (!matched) {
            /* Substring fallback: the COFF string table may have truncated
             * entries like "ta$.refptr.mingw_app_type" where the target name
             * appears as a substring. Only match if the name is long enough
             * (>8 chars) to avoid false positives on short symbols. */
            if (sym_name_len > 8 && strstr(sym_name, name) != NULL) {
                matched = 1;
            }
        }

        if (!matched) continue;

        int32_t section_num = sym->SectionNumber;

        /* Prefer section-bound symbols. If we already have one, skip.
         * For absolute symbols (sec=0), remember as fallback only. */
        if (section_num > 0 && (size_t)section_num <=
            nt->FileHeader.NumberOfSections) {
            if (!has_section_match) {
                IMAGE_SECTION_HEADER *sec = &sections[section_num - 1];
                best_rva = sec->VirtualAddress + sym->Value;
                has_section_match = 1;
            }
        } else if (section_num == 0 && !has_section_match) {
            /* Fallback: absolute symbol, but only if no section-bound one */
            best_rva = sym->Value;
        }
    }

    munmap(file_map, st.st_size);
    return best_rva;
}

void patch_crt_refptrs(const char *file_path, void *image_base,
                       IMAGE_NT_HEADERS64 *nt, IMAGE_SECTION_HEADER *sections)
{
    if (!image_base || !nt || !sections) return;

    g_crt_ctx.image_base = (uint64_t)(uintptr_t)image_base;

    IMAGE_SECTION_HEADER *bss_sec = find_section_by_name(nt, sections, ".bss");
    if (bss_sec) {
        g_crt_ctx.bss_vaddr = bss_sec->VirtualAddress;
        __imp___initenv_stub = (void **)((char *)image_base +
                                          g_crt_ctx.bss_vaddr + CRT_BSS_INITENV);
        fprintf(stderr, "patch_crt_refptrs: .bss at VA=0x%lx, __imp___initenv_stub=%p\n",
                (unsigned long)g_crt_ctx.bss_vaddr, (void *)__imp___initenv_stub);
    } else {
        g_crt_ctx.bss_vaddr = 0;
    }

    /* Discover CRT offsets (argc/argv/envp) from COFF symbol table */
    g_crt_ctx.argc_bss_offset = 0;
    g_crt_ctx.argv_bss_offset = 0;
    g_crt_ctx.envp_bss_offset = 0;

    const char *crt_sym_names[][2] = {
        { "_argc", "__argc" },
        { "_argv", "__argv" },
        { "_environ", "__envp" },
    };
    uint32_t *offset_targets[3] = {
        &g_crt_ctx.argc_bss_offset,
        &g_crt_ctx.argv_bss_offset,
        &g_crt_ctx.envp_bss_offset,
    };

    for (int ci = 0; ci < 3; ci++) {
        for (int ni = 0; ni < 2; ni++) {
            uint64_t rva = find_symbol_rva_from_file(file_path, nt, sections, crt_sym_names[ci][ni]);
            if (rva != 0) {
                uint32_t off = (uint32_t)(rva - g_crt_ctx.bss_vaddr);
                *offset_targets[ci] = off;
                break;
            }
        }
    }

    /* Fallback: if COFF lookup failed, use hardcoded mingw-w64 defaults */
    if (g_crt_ctx.argc_bss_offset == 0 || g_crt_ctx.argv_bss_offset == 0 || g_crt_ctx.envp_bss_offset == 0) {
        fprintf(stderr, "WARNING: COFF symbol lookup for argc/argv/envp incomplete, using hardcoded CRT offsets (0x%x/0x%x/0x%x)\n", CRT_BSS_INITENV, CRT_BSS_ARGV, CRT_BSS_ARGC);
        if (g_crt_ctx.argc_bss_offset == 0) g_crt_ctx.argc_bss_offset = CRT_BSS_ARGC;
        if (g_crt_ctx.argv_bss_offset == 0) g_crt_ctx.argv_bss_offset = CRT_BSS_ARGV;
        if (g_crt_ctx.envp_bss_offset == 0) g_crt_ctx.envp_bss_offset = CRT_BSS_INITENV;
    }

    fprintf(stderr, "patch_crt_refptrs: CRT offsets argc=0x%x argv=0x%x envp=0x%x\n",
            g_crt_ctx.argc_bss_offset, g_crt_ctx.argv_bss_offset, g_crt_ctx.envp_bss_offset);

    uint64_t image_size = nt->OptionalHeader.SizeOfImage;
    int patched_any = 0;
    int patched_initenv = 0;

    /* ── Primary: COFF symbol table ── */
    for (size_t i = 0; i < REF_MAP_COUNT; i++) {
        const refptr_mapping_t *map = &refptr_mappings[i];
        uint64_t target_rva = 0;

        if (file_path) {
            target_rva = find_symbol_rva_from_file(file_path,
                                                    nt, sections, map->name);
        }

        if (target_rva != 0) {
            if (!patched_any)
                fprintf(stderr, "patch_crt_refptrs: using COFF symbol table\n");
            apply_refptr_patch(image_base, target_rva, map->target,
                               map->name, image_size);
            patched_any = 1;
            if (strstr(map->name, "initenv")) patched_initenv = 1;
        }
    }

    /* ── Fallback: scan data sections for refptrs to .bss when COFF fails ──
     * Some mingw-w64 builds don't include certain CRT symbols in the COFF
     * symbol table (only in DWARF). Scan data sections for 8-byte values
     * pointing into .bss and patch them to our stubs. */
    if (bss_sec && bss_sec->Misc.VirtualSize > 0) {
        uint64_t bss_start = bss_sec->VirtualAddress;
        uint64_t bss_end = bss_start + bss_sec->Misc.VirtualSize;

        /* Find which mappings still need patching */
        bool patched[REF_MAP_COUNT];
        memset(patched, 0, sizeof(patched));
        for (size_t i = 0; i < REF_MAP_COUNT; i++) {
            uint64_t trva = find_symbol_rva_from_file(file_path,
                                                     nt, sections,
                                                     refptr_mappings[i].name);
            patched[i] = (trva != 0);
        }

        /* Scan .rdata and .data for 8-byte values pointing into .bss */
        const char *scan_names[] = {".rdata", ".data", NULL};
        int next_unpatched = 0;
        for (int si = 0; scan_names[si]; si++) {
            IMAGE_SECTION_HEADER *scan_sec = find_section_by_name(nt, sections, scan_names[si]);
            if (!scan_sec) continue;
            uint64_t sec_vaddr = scan_sec->VirtualAddress;
            uint64_t sec_size = scan_sec->Misc.VirtualSize;
            if (sec_size == 0) sec_size = scan_sec->SizeOfRawData;

            for (uint64_t off = 0; off + 8 <= sec_size; off += 8) {
                uint64_t *entry = (uint64_t *)((char *)image_base + sec_vaddr + off);
                uint64_t val = *entry;

                /* Check if this entry points into .bss */
                if (val >= (uint64_t)(uintptr_t)image_base + bss_start &&
                    val < (uint64_t)(uintptr_t)image_base + bss_end) {

                    /* Find next unpatched mapping that's a .bss variable
                     * Heuristic: skip __CTOR/__DTOR/__xi/__xc which are .CRT */
                    for (size_t mi = next_unpatched; mi < REF_MAP_COUNT; mi++) {
                        if (patched[mi]) continue;
                        const refptr_mapping_t *map = &refptr_mappings[mi];
                        if (strstr(map->name, "__CTOR") || strstr(map->name, "__DTOR") ||
                            strstr(map->name, "__xi_") || strstr(map->name, "__xc_"))
                            continue;

                        apply_refptr_patch(image_base, sec_vaddr + off,
                                           map->target, map->name, image_size);
                        patched[mi] = true;
                        next_unpatched = (int)(mi + 1);
                        patched_any = 1;
                        if (strstr(map->name, "initenv")) patched_initenv = 1;
                        break;
                    }
                }
            }
        }
    }

    /* ── Always supplement with .text scanning ── */
    if (!patched_initenv) {
        fprintf(stderr, "patch_crt_refptrs: __imp___initenv not in COFF, scanning .text\n");
        scan_text_for_refptrs(image_base, nt, sections, image_size);
    }
}
