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

#include "include/common.h"
#include "include/debug.h"
#include "msvcrt_priv.h"

#define CRT_BSS_INITIALIZED 0x30

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

struct refptr_patch_arg {
    uint64_t *refptr;
    void *target;
    const char *name;
    uint64_t rva;
};

static void refptr_patch_cb(void *arg)
{
    struct refptr_patch_arg *a = (struct refptr_patch_arg *)arg;
    uint64_t old_val = (uint64_t)(uintptr_t)*a->refptr;
    *a->refptr = (uint64_t)(uintptr_t)a->target;
    DEBUG("patch_crt_refptrs: %s at rva 0x%lx: 0x%lx -> %p",
            a->name, (unsigned long)a->rva, old_val, a->target);
}

void apply_refptr_patch(void *image_base, uint64_t rva, void *target,
                               const char *name, uint64_t image_size)
{
    if (rva >= image_size) {
        return;
    }

    uint64_t *refptr = (uint64_t *)((char *)image_base + rva);
    char *page_start = (char *)((uint64_t)(char *)refptr & ~(uint64_t)PAGE_MASK);

    struct refptr_patch_arg arg = {
        .refptr = refptr,
        .target = target,
        .name = name,
        .rva = rva,
    };

    if (with_mprotect_rw(page_start, PAGE_SIZE, refptr_patch_cb, &arg, PROT_READ) != 0) {
        perror("patch_crt_refptrs: with_mprotect_rw");
    }
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
        DEBUG("patch_crt_refptrs: .bss at VA=0x%lx, __imp___initenv_stub=%p",
                (unsigned long)g_crt_ctx.bss_vaddr, (void *)__imp___initenv_stub);
    } else {
        g_crt_ctx.bss_vaddr = 0;
    }

    /* Discover CRT offsets (argc/argv/envp) from COFF symbol table */
    discover_crt_offsets(file_path, nt, sections);

    /* Set the 'initialized' flag to 1 to skip CRT startup (__do_global_ctors).
     * This is at a fixed offset within .bss (0x30 from .bss start) in mingw-w64 builds.
     * Without this, __main calls __do_global_ctors which can crash due to
     * unpatched __DTOR_LIST__ refptrs or other CRT issues. */
    if (bss_sec) {
        uint32_t *initialized_ptr = (uint32_t *)((char *)image_base +
                                                  g_crt_ctx.bss_vaddr + CRT_BSS_INITIALIZED);
        *initialized_ptr = 1;
        DEBUG("patch_crt_refptrs: set initialized=1 at %p", (void *)initialized_ptr);
    }

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
                DEBUG("patch_crt_refptrs: using COFF symbol table");
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
        DEBUG("patch_crt_refptrs: __imp___initenv not in COFF, scanning .text");
        scan_text_for_refptrs(image_base, nt, sections, image_size);
    }
}
