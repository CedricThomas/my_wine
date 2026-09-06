/*
 * import_resolve.c — IAT resolution (pass 1 + pass 2 + thunk strategies)
 *
 * Resolves imports by patching IAT entries in the PE image.
 * Glibc-free: all string/memory ops are hand-rolled. No PLT calls.
 * Binary search in import table is hand-rolled (no bsearch dependency).
 *
 * IMPORTANT: This file runs AFTER the GS→TEB switch in 32-bit mode.
 * No glibc calls permitted (no DEBUG/fprintf, no string.h, etc.)
 */

#include "include/pe.h"
#include "include/pe_parser.h"
#include "src/pe_priv.h"
#include "include/common.h"
#include "include/nt_constants.h"
#include "loader_priv.h"
#include "import_lookup.h"
#include "export_table.h"
#include "module_list.h"
#include "peb_ldr.h"
#include "../syscall/syscalls_inline.h"
#include "include/syscall_safe_utils.h"
#include "dll_path.h"
#include "dll_loader.h"

#define MAX_IMPORT_DEPTH 8

static int image_cstr_valid(void *base, IMAGE_NT_HEADERS *nt, uint32_t rva)
{
    size_t image_size = pe_size_of_image(nt);
    const char *s;

    if (!pe_rva_range_is_valid(rva, 1, image_size))
        return 0;
    s = (const char *)base + rva;
    for (size_t off = rva; off < image_size; off++) {
        if (*s++ == '\0')
            return 1;
    }
    return 0;
}

/**
 * Find the .text jmp-thunk address whose IAT entry resolves to target_addr.
 */
void *find_text_thunk(void *image_base, IMAGE_NT_HEADERS *nt,
                       IMAGE_SECTION_HEADER *sections,
                       void *target_addr)
{
    return find_rip_relative_jump_to(image_base, nt, sections,
                                      pe_section_count(nt),
                                      target_addr);
}

/**
 * Pass 1: resolve import names and write to the descriptor's FirstThunk (IAT).
 */
static int resolve_import_pass1(void *base, IMAGE_NT_HEADERS *nt)
{
    IMAGE_DATA_DIRECTORY imp_dir;
    if (!pe_get_import_dir(nt, &imp_dir)) {
        return 0;
    }
    if (imp_dir.Size == 0) {
        return 0;
    }

    if (imp_dir.VirtualAddress == 0 ||
        !pe_rva_range_is_valid(imp_dir.VirtualAddress, imp_dir.Size,
                               pe_size_of_image(nt))) {
        return -1;
    }

    uint32_t import_rva = imp_dir.VirtualAddress;
    IMAGE_IMPORT_DESCRIPTOR *desc = pe_rva_to_ptr(base, nt, import_rva,
                                                  sizeof(IMAGE_IMPORT_DESCRIPTOR));
    if (desc == NULL) {
        return -1;
    }
    uint32_t desc_offset = 0;

    bool is32 = pe_is_pe32(nt);
    uint64_t high_bit_mask = is32 ? 0x80000000 : 0x8000000000000000ULL;
    size_t thunk_size = is32 ? sizeof(uint32_t) : sizeof(uint64_t);

    while (desc_offset + sizeof(IMAGE_IMPORT_DESCRIPTOR) <= imp_dir.Size &&
           desc->Name != 0) {
        if (!image_cstr_valid(base, nt, desc->Name))
            return -1;
        const char *dll_name = (const char *)base + desc->Name;
        syscall_safe_debug_write_str(2, "resolve_imports: DLL=", dll_name);

        uint32_t ilt_rva = desc->u1.OriginalFirstThunk != 0
                           ? desc->u1.OriginalFirstThunk
                           : desc->FirstThunk;
        uint8_t *orig_base = pe_rva_to_ptr(base, nt, ilt_rva, thunk_size);
        uint8_t *iat_base  = pe_rva_to_ptr(base, nt, desc->FirstThunk, thunk_size);
        if (orig_base == NULL || iat_base == NULL)
            return -1;
        int i;

        for (i = 0; ; i++) {
            uint64_t thunk_val;
            size_t thunk_off = (size_t)i * thunk_size;
            if (thunk_off / thunk_size != (size_t)i ||
                thunk_off > SIZE_MAX - thunk_size ||
                !pe_rva_range_is_valid(ilt_rva, thunk_off + thunk_size,
                                       pe_size_of_image(nt)) ||
                !pe_rva_range_is_valid(desc->FirstThunk, thunk_off + thunk_size,
                                       pe_size_of_image(nt))) {
                return -1;
            }
            if (is32) {
                thunk_val = (uint32_t)*((uint32_t *)(orig_base + i * thunk_size));
            } else {
                thunk_val = *((uint64_t *)(orig_base + i * thunk_size));
            }
            if (thunk_val == 0) break;

            void *addr = NULL;

            if (thunk_val & high_bit_mask) {
                /* Ordinal import (high bit set) */
                uint16_t ordinal = (uint16_t)(thunk_val & 0xFFFF);
                const char *func_name = ordinal_lookup(dll_name, ordinal);
                if (func_name != NULL) {
                    addr = resolve_loader_import(dll_name, func_name);
                    DEBUG_LEVEL(2, "resolve_import ordinal: %s#%u -> %s (%p)",
                                dll_name, (unsigned)ordinal, func_name, addr);
                } else {
                    DEBUG_LEVEL(1, "resolve_import ordinal: %s#%u unresolved name",
                                dll_name, (unsigned)ordinal);
                }
            } else {
                /* Name import */
                if (thunk_val > UINT32_MAX - sizeof(uint16_t) ||
                    !pe_rva_range_is_valid((uint32_t)thunk_val,
                                           sizeof(uint16_t) + 1,
                                           pe_size_of_image(nt)) ||
                    !image_cstr_valid(base, nt,
                                      (uint32_t)thunk_val + sizeof(uint16_t))) {
                    return -1;
                }
                IMAGE_IMPORT_BY_NAME *imp_name =
                    pe_rva_to_ptr(base, nt, (uint32_t)thunk_val,
                                  sizeof(IMAGE_IMPORT_BY_NAME));
                if (imp_name == NULL)
                    return -1;
                addr = resolve_loader_import(dll_name, (const char *)imp_name->Name);
            }

            if (addr != NULL) {
                /* Save index before any block-local shadows */
                const int idx = i;

                /* Debug: log pre-write state */
                if (g_debug_level >= 3)
                {
                    char buf[256];
                    int n = 0;
                    const char *p;
                    for (p = "IAT write: idx="; *p && n < 240; ) buf[n++] = *p++;
                    { int d = n; int v = idx;
                      if (v >= 0) { buf[d++] = '0' + v % 10; v /= 10; }
                      if (v >= 0) { buf[d++] = '0' + v % 10; v /= 10; }
                      if (v >= 0) { buf[d++] = '0' + v % 10; v /= 10; }
                      if (v >= 0) { buf[d++] = '0' + v % 10; v /= 10; }
                      n = d;
                    }
                    for (p = ", dll="; *p && n < 240; ) buf[n++] = *p++;
                    for (p = dll_name; *p && n < 245; ) buf[n++] = *p++;
                    for (p = ", ILT=0x"; *p && n < 245; ) buf[n++] = *p++;
                    syscall_safe_format_hex(buf + n, (uintptr_t)(orig_base + idx * thunk_size), 8); n += 8;
                    for (p = ", IAT=0x"; *p && n < 245; ) buf[n++] = *p++;
                    syscall_safe_format_hex(buf + n, (uintptr_t)(iat_base + idx * thunk_size), 8); n += 8;
                    buf[n++] = '\n';
                    INLINE_SYSCALL_WRITE(2, buf, n);
                }

                if (is32) {
                    *(uint32_t *)(iat_base + idx * thunk_size) = (uint32_t)(uintptr_t)addr;
                } else {
                    *(uint64_t *)(iat_base + idx * thunk_size) = (uint64_t)(uintptr_t)addr;
                }

                /* Debug: readback immediately after write */
                if (g_debug_level >= 3)
                {
                    char buf[256];
                    int n = 0;
                    const char *p;
                    uint64_t readback;
                    if (is32) {
                        readback = (uint32_t)*((uint32_t *)(iat_base + idx * thunk_size));
                    } else {
                        readback = *((uint64_t *)(iat_base + idx * thunk_size));
                    }
                    for (p = "  IAT written=0x"; *p && n < 245; ) buf[n++] = *p++;
                    syscall_safe_format_hex(buf + n, (uintptr_t)addr, 8); n += 8;
                    for (p = ", readback=0x"; *p && n < 245; ) buf[n++] = *p++;
                    syscall_safe_format_hex(buf + n, (uintptr_t)readback, 8); n += 8;
                    if (is32) {
                        for (p = ", offset="; *p && n < 245; ) buf[n++] = *p++;
                        { int d = n; int v = (int)(iat_base + idx * thunk_size - (uint8_t *)base);
                          if (v >= 0) { buf[d++] = '0' + v % 10; v /= 10; }
                          if (v >= 0) { buf[d++] = '0' + v % 10; v /= 10; }
                          if (v >= 0) { buf[d++] = '0' + v % 10; v /= 10; }
                          if (v >= 0) { buf[d++] = '0' + v % 10; v /= 10; }
                          n = d;
                        }
                    }
                    buf[n++] = '\n';
                    INLINE_SYSCALL_WRITE(2, buf, n);
                }
            }
        }

        desc_offset += sizeof(IMAGE_IMPORT_DESCRIPTOR);
        desc = pe_rva_to_ptr(base, nt, import_rva + desc_offset,
                             sizeof(IMAGE_IMPORT_DESCRIPTOR));
        if (desc == NULL)
            return -1;
    }

    if (desc_offset + sizeof(IMAGE_IMPORT_DESCRIPTOR) > imp_dir.Size)
        return -1;

    return 0;
}

/**
 * Scan .text for "ff 25" (jmp *disp32(%rip)) instructions, collect and
 * deduplicate unique IAT target addresses, sort by address.
 */
static int collect_thunk_targets(void *base, IMAGE_NT_HEADERS *nt,
                                 uint64_t targets[MAX_THUNK_TARGETS])
{
    const IMAGE_SECTION_HEADER *sections = get_image_sections(base, nt);

    return scan_rip_relative_jumps(base, nt, sections,
                                    pe_section_count(nt),
                                    targets, MAX_THUNK_TARGETS);
}

/**
 * Match thunk IAT targets against the flat import array and patch mismatches.
 */
static int patch_thunk_targets(void *base, IMAGE_NT_HEADERS *nt,
                               uint64_t *targets, int num_targets,
                               struct import_flat *flat, int num_flat)
{
    bool is32 = pe_is_pe32(nt);
    size_t thunk_size = is32 ? sizeof(uint32_t) : sizeof(uint64_t);
    int matched = 0;
    for (int t = 0; t < num_targets; t++) {
        uint64_t target = targets[t];
        if (target > UINT32_MAX)
            continue;
        void *target_ptr = pe_rva_to_ptr(base, nt, (uint32_t)target, thunk_size);
        if (target_ptr == NULL)
            continue;
        uint64_t current_val;
        if (is32) {
            current_val = (uint32_t)*((uint32_t *)target_ptr);
        } else {
            current_val = *((uint64_t *)target_ptr);
        }

        int did_match = strategy_resolved_overlap(current_val, target_ptr, flat, num_flat) ||
            strategy_ilt_value_match(target_ptr, current_val, target, thunk_size,
                                     flat, num_flat) ||
            strategy_ilt_offset_match(target_ptr, target, current_val, thunk_size,
                                      flat, num_flat);
        if (did_match) {
            matched++;
        }
    }

    return matched;
}

/**
 * Pass 2: patch thunk IAT targets found by scanning .text
 */
static int resolve_import_pass2(void *base, IMAGE_NT_HEADERS *nt)
{
    if (pe_is_pe32(nt)) {
        return 0;
    }

    IMAGE_DATA_DIRECTORY imp_dir;
    if (!pe_get_import_dir(nt, &imp_dir)) {
        return 0;
    }
    if (imp_dir.Size == 0) {
        return 0;
    }

    uint64_t thunk_targets[MAX_THUNK_TARGETS];
    int num_targets = collect_thunk_targets(base, nt, thunk_targets);

    if (num_targets == 0)
        return 0;

    struct import_flat flat[MAX_FLAT_IMPORTS];
    int num_flat = build_flat_import_array(base, nt, flat);

    patch_thunk_targets(base, nt, thunk_targets, num_targets, flat, num_flat);

    return 0;
}

static int is_builtin_stub_library(const char *dll_name)
{
    static const char *const stub_libs[] = {
        "kernel32.dll",
        "ntdll.dll",
        "msvcrt.dll",
        "advapi32.dll",
        "user32.dll",
        "gdi32.dll",
        "winmm.dll",
        "ddraw.dll",
        "dsound.dll",
        "dplay.dll",
        "comdlg32.dll",
        "comctl32.dll",
        NULL
    };
    int i;

    if (dll_name == NULL)
        return 0;

    for (i = 0; stub_libs[i] != NULL; i++) {
        if (syscall_safe_strcasecmp(stub_libs[i], dll_name) == 0)
            return 1;
    }

    return 0;
}

/**
 * Resolve all imports in the PE image.
 */
int resolve_imports(void *base, IMAGE_NT_HEADERS *nt)
{
    syscall_safe_debug_write_ptr(2, "resolve_imports: base=", (uintptr_t)base);
    if (resolve_import_pass1(base, nt) != 0)
        return -1;
    return resolve_import_pass2(base, nt);
}

/**
 * Resolve imports for a dynamically loaded module.
 */
int resolve_module_imports(loaded_module_t *mod, int depth)
{
    if (depth >= MAX_IMPORT_DEPTH) {
        return -1;
    }

    void *base = mod->base;
    IMAGE_NT_HEADERS *nt = mod->nt;

    IMAGE_DATA_DIRECTORY imp_dir;
    if (!pe_get_import_dir(nt, &imp_dir)) {
        return 0; /* No imports */
    }
    if (imp_dir.Size == 0) {
        return 0; /* No imports */
    }

    if (imp_dir.VirtualAddress == 0 ||
        !pe_rva_range_is_valid(imp_dir.VirtualAddress, imp_dir.Size,
                               pe_size_of_image(nt))) {
        return -1;
    }

    uint32_t import_rva = imp_dir.VirtualAddress;
    IMAGE_IMPORT_DESCRIPTOR *desc = pe_rva_to_ptr(base, nt, import_rva,
                                                  sizeof(IMAGE_IMPORT_DESCRIPTOR));
    if (desc == NULL) {
        return -1;
    }

    /* First pass: ensure all dependency DLLs are loaded */
    IMAGE_IMPORT_DESCRIPTOR *d = desc;
    uint32_t desc_offset = 0;
    while (desc_offset + sizeof(IMAGE_IMPORT_DESCRIPTOR) <= imp_dir.Size &&
           d->Name != 0) {
        if (!image_cstr_valid(base, nt, d->Name))
            return -1;
        const char *dll_name = (const char *)base + d->Name;

        /* Check if already loaded */
        loaded_module_t *dep = find_module_by_name(dll_name);
        if (dep == NULL) {
            /* Check if this is a known stub library */
            if (is_builtin_stub_library(dll_name)) {
                desc_offset += sizeof(IMAGE_IMPORT_DESCRIPTOR);
                d = pe_rva_to_ptr(base, nt, import_rva + desc_offset,
                                  sizeof(IMAGE_IMPORT_DESCRIPTOR));
                if (d == NULL)
                    return -1;
                continue;
            }

            /* Need to load this DLL */
            char path[512];
            if (!find_dll_path(dll_name, path, sizeof(path))) {
                return -1;
            }

            /* Load the DLL (map + relocate + register) */
            dep = load_dll(path, depth + 1);
            if (dep == NULL) {
                return -1;
            }
        }
        desc_offset += sizeof(IMAGE_IMPORT_DESCRIPTOR);
        d = pe_rva_to_ptr(base, nt, import_rva + desc_offset,
                          sizeof(IMAGE_IMPORT_DESCRIPTOR));
        if (d == NULL)
            return -1;
    }
    if (desc_offset + sizeof(IMAGE_IMPORT_DESCRIPTOR) > imp_dir.Size)
        return -1;

    /* Second pass: resolve all imports using the three-tier resolver */
    if (resolve_imports(base, nt) != 0) {
        return -1;
    }

    /* Parse exports so this module's functions can be found by others */
    if (parse_export_table(mod) != 0) {
        /* parse_export_table returns -1 if no export dir — that's OK */
    }

    return 0;
}
