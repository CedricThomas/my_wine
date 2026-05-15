/*
 * crt_mingw.c — MinGW-w64 CRT module
 *
 * Implements the crt_module vtable for MinGW-w64 PEs.
 * Extracted from the monolithic crt_refptrs.c / crt_offset_discovery.c / main.c
 *
 * This module handles:
 *  - Detection via COFF symbol table markers (__CTOR_LIST__, __xi_a, __xc_a)
 *  - Refptr patching for MinGW-w64 CRT globals
 *  - BSS offset discovery from COFF symbols with hardcoded fallbacks
 *  - BSS seeding (argc/argv/envp pre-initialization)
 */

#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/stat.h>

#include "include/crt.h"
#include "include/common.h"
#include "include/debug.h"
#include "include/pe_parser.h"
#include "src/pe_priv.h"

#include "crt_priv.h"

/*
 * g_crt is declared in include/crt.h and defined in crt_globals.c.
 * All CRT globals are now fields of g_crt.
 */
#include "../msvcrt/msvcrt_priv.h"

/* ── Helper function declarations (from crt_refptrs.c / crt_offset_discovery.c) */
void apply_refptr_patch(void *image_base, uint64_t rva, void *target,
                        const char *name, uint64_t image_size,
                        uint64_t ctx_image_base, uint64_t ctx_bss_vaddr,
                        IMAGE_NT_HEADERS *nt,
                        IMAGE_SECTION_HEADER *sections);
uint64_t find_symbol_rva_from_file(const char *file_path,
                                   IMAGE_NT_HEADERS *nt,
                                   IMAGE_SECTION_HEADER *sections,
                                   const char *name);
void scan_text_for_refptrs(void *image_base,
                           IMAGE_NT_HEADERS *nt,
                           IMAGE_SECTION_HEADER *sections,
                           uint64_t image_size, void *initenv_stub);



/* ── MinGW BSS layout offsets (relative to .bss base) ────────── */

#define MINGW_BSS_INITENV     0x018   /* __initenv / _environ pointer */
#define MINGW_BSS_ARGV        0x020   /* _argv pointer */
#define MINGW_BSS_ARGC        0x028   /* _argc */
#define MINGW_BSS_INITIALIZED 0x030   /* "initialized" flag — overlaps _acmdln at 0x030 in MinGW layout */

/* ── MinGW entry symbols ──────────────────────────────────────── */

static const char *mingw_entry_symbols[] = {
    "main",
    NULL
};

/* ── Refptr mappings for MinGW-w64 CRT ────────────────────────── */

const refptr_mapping_t mingw_refptr_mappings[] = {
    { "__CTOR_LIST__",              (void *)&g_crt.ctor_list_stub[0] },
    { "__DTOR_LIST__",              (void *)&g_crt.dtor_list_stub[0] },
    { "__xi_a",                     (void *)&g_crt.xi_a_stub },
    { "__dyn_tls_init_callback",    (void *)&g_crt.dyn_tls_callback_stub },
    { "__image_base__",             (void *)&g_crt.crt_ctx.image_base },
    { "__imp___initenv",            (void *)&g_crt.imp_initenv_stub },
    { "__mingw_oldexcpt_handler",   (void *)&g_crt.mingw_excpt_handler_stub },
    { "__native_startup_lock",      (void *)&g_crt.native_startup_lock },
    { "__native_startup_state",     (void *)&g_crt.native_startup_state },
    { "__xc_a",                     (void *)&g_crt.xc_a_stub },
    { "__xc_z",                     (void *)&g_crt.xc_z_stub },
    { "__xi_a (dup)",               (void *)&g_crt.xi_a_stub },
    { "__xi_z",                     (void *)&g_crt.xi_z_stub },
    { "_commode",                   (void *)&g_crt.commode },
    { "__imp__acmdln",              (void *)&g_crt.acmdln },
    { "_dowildcard",                (void *)&g_crt.dowildcard },
    { "_fmode",                     (void *)&g_crt.fmode },
    { "_newmode",                   (void *)&g_crt.newmode },
    { "mingw_app_type",             (void *)&g_crt.app_type },
    { NULL, NULL }
};

/* ── Detection ────────────────────────────────────────────────── */

/*
 * mingw_detect — check for MinGW-w64 markers in the PE COFF symbol table.
 *
 * Looks for __CTOR_LIST__, __xi_a, __xc_a which are distinctive MinGW-w64
 * CRT initialization symbols.  Returns 1 if any found, 0 otherwise.
 */
static int mingw_detect(const char *file_path, IMAGE_NT_HEADERS *nt)
{
    if (!file_path || !nt)
        return 0;

    /*
     * We need section headers to search for marker symbols via
     * find_symbol_rva_from_file, but we don't have a mapped image base.
     * Map the file read-only, get sections, search, then unmap.
     */
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
    if (!sections) {
        munmap(file, file_size);
        return 0;
    }

    const char *markers[] = { "__CTOR_LIST__", "__xi_a", "__xc_a" };
    for (int i = 0; i < 3; i++) {
        uint64_t rva = find_symbol_rva_from_file(file_path, nt,
                                                  sections, markers[i]);
        if (rva != 0) {
            munmap(file, file_size);
            return 1;
        }
    }

    munmap(file, file_size);
    return 0;
}

/* ── Offset discovery ─────────────────────────────────────────── */

/*
 * mingw_discover_offsets — port of discover_crt_offsets() from
 * crt_offset_discovery.c, adapted for the module interface.
 *
 * Looks up _argc/__argc, _argv/__argv, _environ/__envp symbols via COFF
 * symbol table. Falls back to hardcoded MINGW_BSS_* offsets if incomplete.
 */
static void mingw_discover_offsets(const char *file_path,
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

    for (int ci = 0; ci < 3; ci++) {
        for (int ni = 0; ni < 2; ni++) {
            uint64_t rva = find_symbol_rva_from_file(file_path, nt, sections,
                                                     crt_sym_names[ci][ni]);
            if (rva != 0) {
                uint32_t off = (uint32_t)(rva - ctx->bss_vaddr);
                *offset_targets[ci] = off;
                break;
            }
        }
    }

    /* Fallback: if COFF lookup failed, use hardcoded MinGW BSS offsets */
    if (ctx->argc_bss_offset == 0 || ctx->argv_bss_offset == 0 ||
        ctx->envp_bss_offset == 0) {
        DEBUG_LEVEL(1, "WARNING: COFF symbol lookup for argc/argv/envp incomplete, "
              "using hardcoded MinGW BSS offsets (0x%x/0x%x/0x%x)",
              MINGW_BSS_INITENV, MINGW_BSS_ARGV, MINGW_BSS_ARGC);
        if (ctx->argc_bss_offset == 0)
            ctx->argc_bss_offset = MINGW_BSS_ARGC;
        if (ctx->argv_bss_offset == 0)
            ctx->argv_bss_offset = MINGW_BSS_ARGV;
        if (ctx->envp_bss_offset == 0)
            ctx->envp_bss_offset = MINGW_BSS_INITENV;
    }

    DEBUG_LEVEL(2, "mingw_discover_offsets: CRT offsets argc=0x%x argv=0x%x envp=0x%x",
          ctx->argc_bss_offset, ctx->argv_bss_offset, ctx->envp_bss_offset);
}

/* ── Refptr patching ──────────────────────────────────────────── */

/*
 * mingw_patch_refptrs — port of patch_crt_refptrs() from crt_refptrs.c,
 * adapted for the module interface.
 *
 * 1. Finds .bss section and computes initenv_stub address
 * 2. Calls mingw_discover_offsets() to populate ctx
 * 3. Syncs ctx into g_crt.crt_ctx
 * 4. Sets initialized=1 flag in .bss to skip __do_global_ctors
 * 5. Iterates mingw_refptr_mappings, patches via COFF symbol lookup
 * 6. Falls back to scanning .rdata/.data for unpatched .bss refptrs
 * 7. Scans .text for __imp___initenv if not found in COFF
 */
static void mingw_patch_refptrs(const char *file_path, void *image_base,
                                 IMAGE_NT_HEADERS *nt,
                                 IMAGE_SECTION_HEADER *sections)
{
    if (!image_base || !nt || !sections) return;

    /* Build local context; avoid reading global CRT state during patching. */
    crt_context_t ctx = {
        .image_base = (uint64_t)(uintptr_t)image_base,
        .bss_vaddr = 0,
        .argc_bss_offset = 0,
        .argv_bss_offset = 0,
        .envp_bss_offset = 0,
    };

    const IMAGE_SECTION_HEADER *bss_sec = find_section_by_name(nt, sections, ".bss");
    void *initenv_stub = NULL;
    if (bss_sec) {
        ctx.bss_vaddr = bss_sec->VirtualAddress;
        initenv_stub = (void **)((char *)image_base + ctx.bss_vaddr +
                                 MINGW_BSS_INITENV);
        DEBUG_LEVEL(2, "mingw_patch_refptrs: .bss at VA=0x%lx, initenv_stub=%p",
              (unsigned long)ctx.bss_vaddr, (void *)initenv_stub);
    }

    /* Discover CRT offsets (argc/argv/envp) from COFF symbol table */
    mingw_discover_offsets(file_path, nt, sections, &ctx);

    /* Sync local context into g_crt.crt_ctx for later runtime use */
    g_crt.crt_ctx = ctx;

    /* Set the 'initialized' flag to 1 to skip CRT startup (__do_global_ctors).
     * This is at a fixed offset within .bss (0x30 from .bss start) in
     * mingw-w64 builds. Without this, __main calls __do_global_ctors which
     * can crash due to unpatched __DTOR_LIST__ refptrs or other CRT issues. */
    if (bss_sec) {
        uint32_t *initialized_ptr = (uint32_t *)((char *)image_base +
                                                  ctx.bss_vaddr +
                                                  MINGW_BSS_INITIALIZED);
        *initialized_ptr = 1;
        DEBUG_LEVEL(2, "mingw_patch_refptrs: set initialized=1 at %p",
              (void *)initialized_ptr);
    }

    uint64_t image_size = pe_size_of_image(nt);
    int patched_any = 0;
    int patched_initenv = 0;

    /* ── Primary: COFF symbol table ── */
    for (int i = 0; mingw_refptr_mappings[i].name != NULL; i++) {
        const refptr_mapping_t *map = &mingw_refptr_mappings[i];
        uint64_t target_rva = 0;

        if (file_path) {
            target_rva = find_symbol_rva_from_file(file_path,
                                                    nt, sections, map->name);
        }

        if (target_rva != 0) {
            if (!patched_any)
                DEBUG_LEVEL(2, "mingw_patch_refptrs: using COFF symbol table");
            apply_refptr_patch(image_base, target_rva, map->target,
                               map->name, image_size,
                               ctx.image_base, ctx.bss_vaddr,
                               nt, sections);
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
        bool patched[64];
        memset(patched, 0, sizeof(patched));
        int total_mappings = 0;
        for (int i = 0; mingw_refptr_mappings[i].name != NULL; i++) {
            uint64_t trva = find_symbol_rva_from_file(file_path,
                                                      nt, sections,
                                                      mingw_refptr_mappings[i].name);
            patched[i] = (trva != 0);
            total_mappings++;
        }

        /* Scan .rdata and .data for 8-byte values pointing into .bss */
        const char *scan_names[] = { ".rdata", ".data", NULL };
        int next_unpatched = 0;
        for (int si = 0; scan_names[si]; si++) {
            const IMAGE_SECTION_HEADER *scan_sec = find_section_by_name(nt, sections,
                                                                  scan_names[si]);
            if (!scan_sec) continue;
            uint64_t sec_vaddr = scan_sec->VirtualAddress;
            uint64_t sec_size = scan_sec->Misc.VirtualSize;
            if (sec_size == 0) sec_size = scan_sec->SizeOfRawData;

            for (uint64_t off = 0; off + 8 <= sec_size; off += 8) {
                uint64_t *entry = (uint64_t *)((char *)image_base + sec_vaddr +
                                               off);
                uint64_t val = *entry;

                /* Check if this entry points into .bss */
                if (val >= (uint64_t)(uintptr_t)image_base + bss_start &&
                    val < (uint64_t)(uintptr_t)image_base + bss_end) {

                    /* Find next unpatched mapping that's a .bss variable
                     * Heuristic: skip __CTOR/__DTOR/__xi/__xc which are .CRT */
                    for (int mi = next_unpatched; mi < total_mappings; mi++) {
                        if (patched[mi]) continue;
                        const refptr_mapping_t *map = &mingw_refptr_mappings[mi];
                        if (strstr(map->name, "__CTOR") ||
                            strstr(map->name, "__DTOR") ||
                            strstr(map->name, "__xi_") ||
                            strstr(map->name, "__xc_"))
                            continue;

                        apply_refptr_patch(image_base, sec_vaddr + off,
                                           map->target, map->name, image_size,
                                           ctx.image_base, ctx.bss_vaddr,
                                           nt, sections);
                        patched[mi] = true;
                        next_unpatched = mi + 1;
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
        DEBUG_LEVEL(2, "mingw_patch_refptrs: __imp___initenv not in COFF, scanning .text");
        scan_text_for_refptrs(image_base, nt, sections, image_size,
                              initenv_stub);
    }
}

/* ── BSS seeding ──────────────────────────────────────────────── */

/*
 * mingw_seed_bss — port of seed_bss_vars() from main.c,
 * adapted for the module interface.
 *
 * Uses g_crt.crt_ctx offsets to write argc/argv/envp into the PE's .bss.
 * Writes argc=1, argv=NULL, envp=NULL. Uses mprotect to ensure .bss
 * is writable.
 */
static void mingw_seed_bss(void *image_base, IMAGE_NT_HEADERS *nt,
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
            *(uint32_t *)(bss_base + g_crt.crt_ctx.argv_bss_offset) = 0;
        } else {
            *(uint64_t *)(bss_base + g_crt.crt_ctx.argv_bss_offset) = 0;
        }
        DEBUG_LEVEL(2, ".bss: wrote argv=NULL at offset 0x%x",
              g_crt.crt_ctx.argv_bss_offset);
    } else {
        fprintf(stderr, "WARNING: argv_bss_offset is 0, "
                "skipping argv pre-seed\n");
    }

    if (g_crt.crt_ctx.envp_bss_offset != 0) {
        if (pe_is_pe32(nt)) {
            *(uint32_t *)(bss_base + g_crt.crt_ctx.envp_bss_offset) = 0;
        } else {
            *(uint64_t *)(bss_base + g_crt.crt_ctx.envp_bss_offset) = 0;
        }
        DEBUG_LEVEL(2, ".bss: wrote envp=NULL at offset 0x%x",
              g_crt.crt_ctx.envp_bss_offset);
    } else {
        fprintf(stderr, "WARNING: envp_bss_offset is 0, "
                "skipping envp pre-seed\n");
    }
}

/* ── Module definition ────────────────────────────────────────── */

const crt_module_t crt_module_mingw = {
    .name               = "mingw-w64",
    .type               = CRT_TYPE_MINGW,
    .detect             = mingw_detect,
    .entry_symbols      = mingw_entry_symbols,
    .refptr_mappings    = mingw_refptr_mappings,
    .bss_init_offset    = MINGW_BSS_ARGC,
    .bss_argv_offset    = MINGW_BSS_ARGV,
    .bss_initenv_offset = MINGW_BSS_INITENV,
    .patch_refptrs      = mingw_patch_refptrs,
    .discover_offsets   = mingw_discover_offsets,
    .seed_bss           = mingw_seed_bss,
};
