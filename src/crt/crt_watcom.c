/*
 * crt_watcom.c — Watcom CRT module
 *
 * Implements the crt_module vtable for Watcom-compiled PEs.
 *
 * This module handles:
 *  - Detection via COFF symbol table markers (D_DoomMain, _cstartup, .mmh)
 *  - BSS offset discovery with Watcom PE32 fallback offsets
 *  - BSS seeding (argc/argv/envp pre-initialization)
 *
 * Refptr patching is empty for now — Watcom CRT layout is not yet known.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/stat.h>

#include "include/crt.h"
#include "include/common.h"
#include "include/debug.h"
#include "include/pe.h"
#include "include/pe_parser.h"
#include "src/pe_priv.h"

#include "crt_priv.h"

#ifdef MY_WINE32
extern uint32_t pe32_argv_ptr(void);
extern uint32_t pe32_envp_ptr(void);
#endif

/* g_crt is declared in include/crt.h, defined in crt_globals.c */

/* ── Watcom BSS layout offsets (relative to .bss base) ───────────
 * These are compatibility fallbacks for known Watcom-style PE32 images when
 * symbol lookup cannot recover argc/argv/envp locations. Prefer COFF
 * symbol discovery whenever symbols are present. */
#define WATCOM_BSS_INITENV     0x018   /* __initenv / _environ pointer */
#define WATCOM_BSS_ARGV        0x020   /* _argv pointer */
#define WATCOM_BSS_ARGC        0x028   /* _argc */
#define WATCOM_BSS_INITIALIZED 0x030   /* "initialized" flag — overlaps _acmdln */

/* Symbols safe to use when bypassing Watcom CRT startup.
 * CRT startup symbols such as _cstartup/_startup are detection markers only;
 * jumping to them would re-enter CRT code with a user-entry stack frame. */

static const char *watcom_entry_symbols[] = {
    "D_DoomMain",
    "_D_DoomMain",
    "main",
    NULL
};

const refptr_mapping_t watcom_refptr_mappings[] = {
    { NULL, NULL }
};

/* ── Detection ─────────────────────────────────────────────────── */

/*
 * watcom_detect — check for Watcom CRT markers in the PE.
 *
 * Requires at least one strong indicator:
 *   1. .mmh section (Watcom-specific heap metadata)
 *   2. BEGTEXT + DGROUP section names (both present, Watcom code/data sections)
 *   3. D_DoomMain or _D_DoomMain in the COFF symbol table
 *   4. _cstartup or _startup (Watcom CRT entry symbols)
 *
 * Returns 1 if a strong indicator found, 0 otherwise.
 */
static int watcom_detect(const char *file_path, IMAGE_NT_HEADERS *nt)
{
    if (!file_path || !nt)
        return 0;

    /* Check for .mmh section (Watcom-specific heap metadata) */
    int fd = open(file_path, O_RDONLY);
    if (fd < 0)
        return 0;

    struct stat st;
    if (fstat(fd, &st) < 0) {
        close(fd);
        return 0;
    }
    size_t file_size = (size_t)st.st_size;

    void *file = mmap(NULL, file_size, PROT_READ, MAP_PRIVATE, fd, 0);
    close(fd);
    if (file == MAP_FAILED)
        return 0;

    IMAGE_SECTION_HEADER *sections = get_image_sections(file, nt);
    int has_begtext = 0, has_dgroup = 0;
    if (sections) {
        if (find_section_by_name(nt, sections, ".mmh")) {
            munmap(file, file_size);
            return 1;
        }

        has_begtext = find_section_by_name(nt, sections, "BEGTEXT") != NULL;
        has_dgroup = find_section_by_name(nt, sections, "DGROUP") != NULL;
        if (has_begtext && has_dgroup) {
            munmap(file, file_size);
            return 1;
        }
    }

    /* Check COFF symbol table for Watcom markers */
    IMAGE_SYMBOL *symbols = NULL;
    char *string_table = NULL;
    int count = parse_symbol_table_from_file(file_path, nt, &symbols, &string_table);
    if (count > 0) {
        const char *markers[] = {
            "D_DoomMain", "_D_DoomMain",
            "_cstartup", "_startup"
        };
        uint16_t num_sections = sections ? pe_section_count(nt) : 0;
        for (int i = 0; i < 4; i++) {
            uint32_t rva = lookup_symbol_rva(symbols, count, string_table,
                                             sections, num_sections,
                                             markers[i]);
            if (rva != 0) {
                free(symbols);
                munmap(file, file_size);
                return 1;
            }
        }
        free(symbols);
    }

    /* Detection failed — log if we saw a weak section hint without symbols */
    if ((has_begtext || has_dgroup) && count <= 0) {
        DEBUG_LEVEL(1, "watcom_detect: found Watcom section name(s) but no COFF symbols, "
              "insufficient evidence for Watcom CRT");
    }

    munmap(file, file_size);
    return 0;
}

/* ── Offset discovery ─────────────────────────────────────────── */

/*
 * watcom_discover_offsets — minimal COFF lookup for _argc/_argv/__envp.
 *
 * Same approach as MinGW: try COFF symbol table, then use the
 * Watcom PE32 WATCOM_BSS_* offsets when symbols are absent.
 */
static void watcom_discover_offsets(const char *file_path,
                                     IMAGE_NT_HEADERS *nt,
                                     IMAGE_SECTION_HEADER *sections,
                                     crt_context_t *ctx)
{
    ctx->argc_bss_offset = 0;
    ctx->argv_bss_offset = 0;
    ctx->envp_bss_offset = 0;

    const char *crt_sym_names[][2] = {
        { "_argc", "__argc" },
        { "_argv", "__argv" },
        { "_environ", "__envp" },
    };
    uint32_t *offset_targets[3] = {
        &ctx->argc_bss_offset,
        &ctx->argv_bss_offset,
        &ctx->envp_bss_offset,
    };

    /* Parse COFF symbol table for offset lookup */
    IMAGE_SYMBOL *symbols = NULL;
    char *string_table = NULL;
    int count = parse_symbol_table_from_file(file_path, nt, &symbols, &string_table);

    uint16_t num_sections = sections ? pe_section_count(nt) : 0;
    if (count > 0) {
        for (int ci = 0; ci < 3; ci++) {
            for (int ni = 0; ni < 2; ni++) {
                uint32_t rva = lookup_symbol_rva(symbols, count, string_table,
                                                 sections, num_sections,
                                                 crt_sym_names[ci][ni]);
                if (rva != 0) {
                    uint32_t off = (uint32_t)(rva - ctx->bss_vaddr);
                    *offset_targets[ci] = off;
                    break;
                }
            }
        }
    }
    if (symbols) free(symbols);

    /* Fallback: if COFF lookup failed, use hardcoded Watcom BSS offsets */
    if (ctx->argc_bss_offset == 0 || ctx->argv_bss_offset == 0 ||
        ctx->envp_bss_offset == 0) {
        DEBUG_LEVEL(1, "WARNING: COFF symbol lookup for argc/argv/envp incomplete, "
              "using hardcoded Watcom BSS offsets (0x%x/0x%x/0x%x)",
              WATCOM_BSS_INITENV, WATCOM_BSS_ARGV, WATCOM_BSS_ARGC);
        if (ctx->argc_bss_offset == 0)
            ctx->argc_bss_offset = WATCOM_BSS_ARGC;
        if (ctx->argv_bss_offset == 0)
            ctx->argv_bss_offset = WATCOM_BSS_ARGV;
        if (ctx->envp_bss_offset == 0)
            ctx->envp_bss_offset = WATCOM_BSS_INITENV;
    }

    DEBUG_LEVEL(2, "watcom_discover_offsets: CRT offsets argc=0x%x argv=0x%x envp=0x%x",
          ctx->argc_bss_offset, ctx->argv_bss_offset, ctx->envp_bss_offset);
}

/* ── Refptr patching ──────────────────────────────────────────── */

/*
 * watcom_patch_refptrs — minimal: discovers offsets and stores them in
 * g_crt.crt_ctx. No refptr patching is needed because watcom_refptr_mappings is
 * empty.
 */
static void watcom_patch_refptrs(const char *file_path, void *image_base,
                                 IMAGE_NT_HEADERS *nt,
                                 IMAGE_SECTION_HEADER *sections)
{
    if (!image_base || !nt || !sections) return;

    crt_context_t ctx = {
        .image_base = (uint64_t)(uintptr_t)image_base,
        .bss_vaddr = 0,
        .argc_bss_offset = 0,
        .argv_bss_offset = 0,
        .envp_bss_offset = 0,
    };

    const IMAGE_SECTION_HEADER *bss_sec = find_section_by_name(nt, sections, ".bss");
    if (bss_sec) {
        ctx.bss_vaddr = bss_sec->VirtualAddress;
        DEBUG_LEVEL(2, "watcom_patch_refptrs: .bss at VA=0x%lx",
              (unsigned long)ctx.bss_vaddr);
    }

    /* Discover CRT offsets (argc/argv/envp) from COFF symbol table */
    watcom_discover_offsets(file_path, nt, sections, &ctx);

    /* Sync local context into g_crt.crt_ctx for later runtime use */
    g_crt.crt_ctx = ctx;

    /* Set the 'initialized' flag to 1 to skip CRT startup */
    if (bss_sec) {
        uint8_t *bss_base = (uint8_t *)image_base + ctx.bss_vaddr;

        /* Ensure .bss page is writable (same pattern as watcom_seed_bss) */
        size_t bss_size = bss_sec->Misc.VirtualSize;
        if (bss_size == 0) bss_size = bss_sec->SizeOfRawData;

        uintptr_t bss_page = (uintptr_t)bss_base & ~(uintptr_t)PAGE_MASK;
        if (mprotect((void *)bss_page,
                     (bss_size + PAGE_MASK) & ~(size_t)PAGE_MASK,
                     PROT_READ | PROT_WRITE) != 0) {
            fprintf(stderr, "WARNING: mprotect .bss failed in watcom_patch_refptrs, "
                    "skipping initialized write\n");
        } else {
            uint32_t *initialized_ptr = (uint32_t *)(bss_base +
                                                     WATCOM_BSS_INITIALIZED);
            *initialized_ptr = 1;
            DEBUG_LEVEL(2, "watcom_patch_refptrs: set initialized=1 at %p",
                  (void *)initialized_ptr);
        }
    }

    DEBUG_LEVEL(2, "watcom_patch_refptrs: no refptr mappings to patch (empty table)");
}

/* ── BSS seeding ───────────────────────────────────────────────── */

/*
 * watcom_seed_bss — BSS seeding using g_crt.crt_ctx offsets.
 * Same pattern as MinGW seed_bss.
 */
static void watcom_seed_bss(void *image_base, IMAGE_NT_HEADERS *nt,
                            IMAGE_SECTION_HEADER *sections)
{
    if (g_crt.crt_ctx.bss_vaddr == 0) {
        fprintf(stderr, "WARNING: .bss section not found, "
                "skipping argc/argv/envp pre-seed\n");
        return;
    }

    uint8_t *bss_base = (uint8_t *)image_base + g_crt.crt_ctx.bss_vaddr;

    const IMAGE_SECTION_HEADER *bss_sec = find_section_by_name(nt, sections, ".bss");
    if (bss_sec == NULL) {
        fprintf(stderr, "WARNING: .bss section not found in headers, "
                "skipping pre-seed\n");
        return;
    }

    size_t bss_size = bss_sec->Misc.VirtualSize;
    if (bss_size == 0) bss_size = bss_sec->SizeOfRawData;

    /* Ensure .bss page is writable */
    uintptr_t bss_page = (uintptr_t)bss_base & ~(uintptr_t)PAGE_MASK;
    if (mprotect((void *)bss_page,
                  (bss_size + PAGE_MASK) & ~(size_t)PAGE_MASK,
                  PROT_READ | PROT_WRITE) != 0) {
        fprintf(stderr, "WARNING: mprotect .bss failed, skipping pre-seed\n");
        return;
    }

    if (g_crt.crt_ctx.argc_bss_offset != 0) {
        *(uint32_t *)(bss_base + g_crt.crt_ctx.argc_bss_offset) = 1;
        DEBUG_LEVEL(2, ".bss: wrote argc=1 at offset 0x%x",
              g_crt.crt_ctx.argc_bss_offset);
    } else {
        fprintf(stderr, "WARNING: argc_bss_offset is 0, "
                "skipping argc pre-seed\n");
    }

    if (g_crt.crt_ctx.argv_bss_offset != 0) {
        if (pe_is_pe32(nt)) {
#ifdef MY_WINE32
            *(uint32_t *)(bss_base + g_crt.crt_ctx.argv_bss_offset) = pe32_argv_ptr();
#else
            *(uint32_t *)(bss_base + g_crt.crt_ctx.argv_bss_offset) = 0;
#endif
        } else {
            *(uint64_t *)(bss_base + g_crt.crt_ctx.argv_bss_offset) = 0;
        }
        DEBUG_LEVEL(2, ".bss: wrote argv at offset 0x%x",
              g_crt.crt_ctx.argv_bss_offset);
    } else {
        fprintf(stderr, "WARNING: argv_bss_offset is 0, "
                "skipping argv pre-seed\n");
    }

    if (g_crt.crt_ctx.envp_bss_offset != 0) {
        if (pe_is_pe32(nt)) {
#ifdef MY_WINE32
            *(uint32_t *)(bss_base + g_crt.crt_ctx.envp_bss_offset) = pe32_envp_ptr();
#else
            *(uint32_t *)(bss_base + g_crt.crt_ctx.envp_bss_offset) = 0;
#endif
        } else {
            *(uint64_t *)(bss_base + g_crt.crt_ctx.envp_bss_offset) = 0;
        }
        DEBUG_LEVEL(2, ".bss: wrote envp at offset 0x%x",
              g_crt.crt_ctx.envp_bss_offset);
    } else {
        fprintf(stderr, "WARNING: envp_bss_offset is 0, "
                "skipping envp pre-seed\n");
    }
}

/* ── Module definition ─────────────────────────────────────────── */

const crt_module_t crt_module_watcom = {
    .name               = "watcom",
    .type               = CRT_TYPE_WATCOM,
    .detect             = watcom_detect,
    .entry_symbols      = watcom_entry_symbols,
    .refptr_mappings    = watcom_refptr_mappings,
    .bss_init_offset    = WATCOM_BSS_ARGC,
    .bss_argv_offset    = WATCOM_BSS_ARGV,
    .bss_initenv_offset = WATCOM_BSS_INITENV,
    .patch_refptrs      = watcom_patch_refptrs,
    .discover_offsets   = watcom_discover_offsets,
    .seed_bss           = watcom_seed_bss,
};
