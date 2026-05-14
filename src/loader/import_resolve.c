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
#include "include/pe_priv.h"
#include "include/common.h"
#include "include/nt_constants.h"
#include "loader_priv.h"
#include "export_table.h"
#include "module_list.h"
#include "peb_ldr.h"
#include "../syscall/syscalls_inline.h"
#include "loader_utils.h"
#include "dll_path.h"
#include "dll_loader.h"

/* ── Debug helpers (syscall-safe, no glibc) ── */
static inline void dbg_fmt_hex(char *dst, uintptr_t val)
{
    for (int i = 7; i >= 0; i--) {
        dst[i] = "0123456789abcdef"[val & 0xf];
        val >>= 4;
    }
}

static inline void dbg_write_ptr(int level, const char *prefix, uintptr_t val)
{
    if (g_debug_level < level) return;
    char buf[64];
    int i = 0;
    const char *p;
    for (p = prefix; *p; ) buf[i++] = *p++;
    buf[i++] = '0'; buf[i++] = 'x';
    dbg_fmt_hex(buf + i, val);
    i += 8;
    buf[i++] = '\n';
    INLINE_SYSCALL_WRITE(2, buf, i);
}

static inline void dbg_write_str(int level, const char *prefix, const char *str)
{
    if (g_debug_level < level) return;
    char buf[256];
    int i = 0;
    const char *p;
    for (p = prefix; *p && i < 240; ) buf[i++] = *p++;
    for (p = str; *p && i < 250; ) buf[i++] = *p++;
    buf[i++] = '\n';
    INLINE_SYSCALL_WRITE(2, buf, i);
}

#define MAX_IMPORT_DEPTH 8

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

static void *resolve_import(const char *dll_name, const char *func_name)
{
    /* Tier 1: lookup in our stub import table */
    import_entry_t *entry = NULL;
#if defined(MY_WINE32)
    /* 32-bit: linear scan (avoid sort/bsearch dependency issues) */
    for (size_t i = 0; i < import_table_count; i++) {
        if (import_cmp_by_name(func_name, &import_table[i]) == 0) {
            entry = &import_table[i];
            break;
        }
    }
#else
    /* 64-bit: hand-rolled binary search */
    size_t lo = 0, hi = import_table_count;
    while (lo < hi) {
        size_t mid = lo + (hi - lo) / 2;
        int cmp = import_cmp_by_name(func_name, &import_table[mid]);
        if (cmp < 0) {
            hi = mid;
        } else if (cmp > 0) {
            lo = mid + 1;
        } else {
            entry = &import_table[mid];
            break;
        }
    }
#endif
    if (entry != NULL && entry->address != NULL) {
        if (g_debug_level >= 2)
        { char buf[256]; int i = 0;
          const char *p;
          for (p = "resolve_import: "; *p && i < 250; ) buf[i++] = *p++;
          for (p = dll_name; *p && i < 250; ) buf[i++] = *p++;
          if (i < 250) buf[i++] = '!';
          for (p = func_name; *p && i < 250; ) buf[i++] = *p++;
          if (i < 250) buf[i++] = ' ';
          if (i < 250) buf[i++] = '-';
          if (i < 250) buf[i++] = '>';
          if (i < 250) buf[i++] = ' ';
          if (i < 250) buf[i++] = '0';
          if (i < 250) buf[i++] = 'x';
          dbg_fmt_hex(buf + i, (uintptr_t)entry->address); i += 8;
          if (i < 255) buf[i++] = '\n';
          INLINE_SYSCALL_WRITE(2, buf, i);
        }
        return entry->address;
    }

    /* Tier 2: lookup in loaded module exports */
    loaded_module_t *mod = find_module_by_name(dll_name);
    if (mod != NULL && mod->export_cache.number_of_names > 0) {
        void *addr = lookup_export(mod, func_name);
        if (addr != NULL) {
            if (g_debug_level >= 2)
            { char buf[256]; int i = 0;
              const char *p;
              for (p = "resolve_import: "; *p && i < 250; ) buf[i++] = *p++;
              for (p = dll_name; *p && i < 250; ) buf[i++] = *p++;
              if (i < 250) buf[i++] = '!';
              for (p = func_name; *p && i < 250; ) buf[i++] = *p++;
              if (i < 250) buf[i++] = ' ';
              if (i < 250) buf[i++] = '-';
              if (i < 250) buf[i++] = '>';
              if (i < 250) buf[i++] = ' ';
              if (i < 250) buf[i++] = '0';
              if (i < 250) buf[i++] = 'x';
              dbg_fmt_hex(buf + i, (uintptr_t)addr); i += 8;
              if (i < 255) buf[i++] = '\n';
              INLINE_SYSCALL_WRITE(2, buf, i);
            }
            return addr;
        }
    }

    /* Tier 3: not found */
    if (g_debug_level >= 2)
    { char buf[256]; int i = 0;
      const char *p;
      for (p = "resolve_import: "; *p && i < 250; ) buf[i++] = *p++;
      for (p = dll_name; *p && i < 250; ) buf[i++] = *p++;
      if (i < 250) buf[i++] = '!';
      for (p = func_name; *p && i < 250; ) buf[i++] = *p++;
      if (i < 250) buf[i++] = ' ';
      if (i < 250) buf[i++] = '-';
      if (i < 250) buf[i++] = '>';
      if (i < 250) buf[i++] = ' ';
      const char *notf = "(null)";
      for (p = notf; *p && i < 255; ) buf[i++] = *p++;
      if (i < 255) buf[i++] = '\n';
      INLINE_SYSCALL_WRITE(2, buf, i);
    }
    return NULL;
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

    uint64_t import_rva = imp_dir.VirtualAddress;
    IMAGE_IMPORT_DESCRIPTOR *desc = (IMAGE_IMPORT_DESCRIPTOR *)((char *)base + import_rva);

    bool is32 = pe_is_pe32(nt);
    uint64_t high_bit_mask = is32 ? 0x80000000 : 0x8000000000000000ULL;
    size_t thunk_size = is32 ? sizeof(uint32_t) : sizeof(uint64_t);

    while (desc->Name != 0) {
        const char *dll_name = (const char *)((char *)base + desc->Name);
        dbg_write_str(2, "resolve_imports: DLL=", dll_name);

        uint8_t *orig_base = (uint8_t *)((char *)base + desc->u1.OriginalFirstThunk);
        uint8_t *iat_base  = (uint8_t *)((char *)base + desc->FirstThunk);
        int i;

        for (i = 0; ; i++) {
            uint64_t thunk_val;
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
                    addr = resolve_import(dll_name, func_name);
                }
            } else {
                /* Name import */
                IMAGE_IMPORT_BY_NAME *imp_name = (IMAGE_IMPORT_BY_NAME *)((char *)base + thunk_val);
                addr = resolve_import(dll_name, (const char *)imp_name->Name);
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
                    dbg_fmt_hex(buf + n, (uintptr_t)(orig_base + idx * thunk_size)); n += 8;
                    for (p = ", IAT=0x"; *p && n < 245; ) buf[n++] = *p++;
                    dbg_fmt_hex(buf + n, (uintptr_t)(iat_base + idx * thunk_size)); n += 8;
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
                    dbg_fmt_hex(buf + n, (uintptr_t)addr); n += 8;
                    for (p = ", readback=0x"; *p && n < 245; ) buf[n++] = *p++;
                    dbg_fmt_hex(buf + n, (uintptr_t)readback); n += 8;
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

        desc++;
    }

    return 0;
}

/**
 * Scan .text for "ff 25" (jmp *disp32(%rip)) instructions, collect and
 * deduplicate unique IAT target addresses, sort by address.
 */
static int collect_thunk_targets(void *base, IMAGE_NT_HEADERS *nt,
                                 uint64_t targets[MAX_THUNK_TARGETS])
{
    IMAGE_SECTION_HEADER *sections = get_image_sections(base, nt);

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
        void *target_ptr = (char *)base + target;
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

/**
 * Resolve all imports in the PE image.
 */
int resolve_imports(void *base, IMAGE_NT_HEADERS *nt)
{
    dbg_write_ptr(2, "resolve_imports: base=", (uintptr_t)base);
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

    uint64_t import_rva = imp_dir.VirtualAddress;
    IMAGE_IMPORT_DESCRIPTOR *desc = (IMAGE_IMPORT_DESCRIPTOR *)((char *)base + import_rva);

    /* First pass: ensure all dependency DLLs are loaded */
    IMAGE_IMPORT_DESCRIPTOR *d = desc;
    while (d->Name != 0) {
        const char *dll_name = (const char *)((char *)base + d->Name);

        /* Check if already loaded */
        loaded_module_t *dep = find_module_by_name(dll_name);
        if (dep == NULL) {
            /* Check if this is a known stub library */
            if (dll_strcasecmp("kernel32.dll", dll_name) == 0 ||
                dll_strcasecmp("ntdll.dll", dll_name) == 0 ||
                dll_strcasecmp("msvcrt.dll", dll_name) == 0) {
                d++;
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
        d++;
    }

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
