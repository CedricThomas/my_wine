/*
 * crt_refptrs.c — CRT refptr patching (mapping + orchestration).
 *
 * Patches PE refptr entries so that CRT startup code doesn't crash
 * on two-level indirection through unmapped addresses.
 * Offset discovery is in crt_offset_discovery.c.
 */

#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include <sys/mman.h>

#include "include/crt.h"
#include "include/common.h"
#include "include/debug.h"
#include "src/pe_priv.h"
#include "msvcrt_priv.h"

#define CRT_BSS_INITIALIZED 0x30
#define CRT_BSS_INITENV     0x018   /* __initenv / _environ pointer in .bss */

/*
 * refptr_mappings: patch targets for CRT refptr entries.
 *
 * The __image_base__ entry stores &g_crt.crt_ctx.image_base as the target
 * address. This is a compile-time address computation (not a read), so there
 * is no data race even under concurrent patching. g_crt.crt_ctx is only
 * written AFTER all patching completes (via g_crt.crt_ctx = ctx in
 * patch_crt_refptrs), so the CRT runtime will always read the correct,
 * finalized value.
 */
const refptr_mapping_t refptr_mappings[] = {
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
    { "__imp__acmdln",          (void *)&g_crt.acmdln },
    { "_dowildcard",                (void *)&g_crt.dowildcard },
    { "_fmode",                     (void *)&g_crt.fmode },
    { "_newmode",                   (void *)&g_crt.newmode },
    { "mingw_app_type",             (void *)&g_crt.app_type },
    { NULL, NULL }
};

#define REF_MAP_COUNT (sizeof(refptr_mappings) / sizeof(refptr_mappings[0]) - 1)

struct refptr_patch_arg {
    uint64_t *refptr;
    void *target;
    const char *name;
    uint64_t rva;
    uint64_t image_base;   /* image base for reentrant context */
    uint64_t bss_vaddr;    /* .bss VA for reentrant context */
};

static void refptr_patch_cb(void *arg)
{
    struct refptr_patch_arg *a = (struct refptr_patch_arg *)arg;
    uint64_t old_val = (uint64_t)(uintptr_t)*a->refptr;
    *a->refptr = (uint64_t)(uintptr_t)a->target;
    DEBUG_LEVEL(2, "patch_crt_refptrs: %s at rva 0x%lx: 0x%lx -> %p",
            a->name, (unsigned long)a->rva, old_val, a->target);
}

void apply_refptr_patch(void *image_base, uint64_t rva, void *target,
                               const char *name, uint64_t image_size,
                               uint64_t ctx_image_base, uint64_t ctx_bss_vaddr,
                               IMAGE_NT_HEADERS *nt,
                               IMAGE_SECTION_HEADER *sections)
{
    if (rva >= image_size) {
        return;
    }

    uint64_t *refptr = (uint64_t *)((char *)image_base + rva);
    char *page_start = (char *)((uint64_t)(char *)refptr & ~(uint64_t)PAGE_MASK);

    /* Compute the correct restore_prot from the section that contains this refptr.
     * For PE32, refptrs in .idata (which is R+W) must be restored to PROT_READ|PROT_WRITE
     * so that resolve_imports can later write to the IAT in the same page.
     * Hardcoding PROT_READ (the previous behavior) would leave .idata read-only and
     * crash when resolve_imports tries to write IAT entries. */
    int restore_prot = PROT_READ;  /* fallback */
    if (nt && sections) {
        /* Convert page_start to an RVA for comparison against section RVAs */
        uint64_t page_rva = (uint64_t)(uintptr_t)page_start - (uint64_t)(uintptr_t)image_base;
        for (uint16_t i = 0; i < pe_section_count(nt); i++) {
            uint64_t sec_start = sections[i].VirtualAddress;
            uint64_t sec_end = sec_start +
                (sections[i].Misc.VirtualSize > 0
                 ? sections[i].Misc.VirtualSize
                 : sections[i].SizeOfRawData);
            if (page_rva >= sec_start && page_rva < sec_end) {
                restore_prot = 0;
                if (sections[i].Characteristics & IMAGE_SCN_MEM_READ)
                    restore_prot |= PROT_READ;
                if (sections[i].Characteristics & IMAGE_SCN_MEM_WRITE)
                    restore_prot |= PROT_WRITE;
                if (sections[i].Characteristics & IMAGE_SCN_MEM_EXECUTE)
                    restore_prot |= PROT_EXEC;
                break;
            }
        }
    }

    struct refptr_patch_arg arg = {
        .refptr = refptr,
        .target = target,
        .name = name,
        .rva = rva,
        .image_base = ctx_image_base,
        .bss_vaddr = ctx_bss_vaddr,
    };

    if (with_mprotect_rw(page_start, PAGE_SIZE, refptr_patch_cb, &arg, restore_prot) != 0) {
        perror("patch_crt_refptrs: with_mprotect_rw");
    }
}

void patch_crt_refptrs(const char *file_path, void *image_base,
                       IMAGE_NT_HEADERS *nt, IMAGE_SECTION_HEADER *sections)
{
    if (!image_base || !nt || !sections) return;

    /* Use the already-detected CRT module. If the module provides
     * a patch_refptrs vtable entry, delegate entirely — no fallback needed.
     * Otherwise, fall through to the inline logic below. */
    const crt_module_t *mod = crt_get_active();
    if (mod) {
        crt_patch_refptrs(mod, file_path, image_base, nt, sections);
        return;
    }

    /* ── Fallback: inline patching when no module or vtable entry is available ── */

    /* Build local context — avoids reading g_crt.crt_ctx during patching */
    crt_context_t ctx = {
        .image_base = (uint64_t)(uintptr_t)image_base,
        .bss_vaddr = 0,
        .argc_bss_offset = 0,
        .argv_bss_offset = 0,
        .envp_bss_offset = 0,
    };

    IMAGE_SECTION_HEADER *bss_sec = find_section_by_name(nt, sections, ".bss");
    void *initenv_stub = NULL;
    if (bss_sec) {
        ctx.bss_vaddr = bss_sec->VirtualAddress;
        initenv_stub = (void **)((char *)image_base + ctx.bss_vaddr + CRT_BSS_INITENV);
        DEBUG_LEVEL(2, "patch_crt_refptrs: .bss at VA=0x%lx, initenv_stub=%p",
                (unsigned long)ctx.bss_vaddr, (void *)initenv_stub);
    }

    /* Discover CRT offsets (argc/argv/envp) from COFF symbol table */
    discover_crt_offsets(file_path, nt, sections, &ctx);

    /* Sync local context into g_crt.crt_ctx for later runtime use */
    g_crt.crt_ctx = ctx;

    /* Set the 'initialized' flag to 1 to skip CRT startup (__do_global_ctors).
     * This is at a fixed offset within .bss (0x30 from .bss start) in mingw-w64 builds.
     * Without this, __main calls __do_global_ctors which can crash due to
     * unpatched __DTOR_LIST__ refptrs or other CRT issues. */
    if (bss_sec) {
        uint32_t *initialized_ptr = (uint32_t *)((char *)image_base +
                                                  ctx.bss_vaddr + CRT_BSS_INITIALIZED);
        *initialized_ptr = 1;
        DEBUG_LEVEL(2, "patch_crt_refptrs: set initialized=1 at %p", (void *)initialized_ptr);
    }

    uint64_t image_size = pe_size_of_image(nt);
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
                DEBUG_LEVEL(2, "patch_crt_refptrs: using COFF symbol table");
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
                                           map->target, map->name, image_size,
                                           ctx.image_base, ctx.bss_vaddr,
                                           nt, sections);
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
        DEBUG_LEVEL(2, "patch_crt_refptrs: __imp___initenv not in COFF, scanning .text");
        scan_text_for_refptrs(image_base, nt, sections, image_size, initenv_stub);
    }
}
