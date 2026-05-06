/*
 * import_resolve.c — IAT resolution (pass 1 + pass 2 + thunk strategies)
 *
 * Resolves imports by patching IAT entries in the PE image.
 * Glibc-free: all string/memory ops are hand-rolled.
 * Binary search in import table is hand-rolled (no bsearch dependency).
 */

#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/mman.h>

#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/mman.h>

#include "include/pe.h"
#include "include/pe_parser.h"
#include "include/common.h"
#include "include/nt_constants.h"
#include "loader_priv.h"
#include "include/debug.h"
#include "export_table.h"
#include "module_list.h"
#include "peb_ldr.h"
#include "../syscalls_inline.h"

/* For extern environ — avoid getenv() in syscall-safe path */
extern char **environ;

#define MAX_IMPORT_DEPTH 8

/* ── Hand-rolled helpers (no glibc) ─────────────────────────────── */

static int dll_strcasecmp(const char *a, const char *b)
{
    while (*a && *b) {
        unsigned char ca = *a, cb = *b;
        if (ca >= 'A' && ca <= 'Z') ca += 32;
        if (cb >= 'A' && cb <= 'Z') cb += 32;
        if (ca != cb) return (int)ca - (int)cb;
        a++; b++;
    }
    unsigned char ca = *a, cb = *b;
    if (ca >= 'A' && ca <= 'Z') ca += 32;
    if (cb >= 'A' && cb <= 'Z') cb += 32;
    return (int)ca - (int)cb;
}

/* Case-insensitive string equality */
static int strci_equal(const char *a, const char *b)
{
    return dll_strcasecmp(a, b) == 0;
}

/* Forward declarations */
int find_dll_path(const char *dll_name, char *path, size_t path_size);
loaded_module_t *load_dll(const char *path, int depth);
static void init_exe_dir(void);

static char g_exe_dir[512] = {0};

/**
 * Initialize g_exe_dir with the current working directory (app directory).
 * Called once on first use. Matches Windows behavior where the app directory
 * is searched for DLLs.
 */
static void init_exe_dir(void)
{
    if (g_exe_dir[0] != '\0') return;
    const char *pe_path = get_pe_path();
    if (pe_path != NULL && pe_path[0] != '\0') {
        /* Hand-rolled strrchr */
        const char *last_slash = NULL;
        const char *p = pe_path;
        while (*p) {
            if (*p == '/') last_slash = p;
            p++;
        }
        if (last_slash != NULL && last_slash != pe_path) {
            size_t dir_len = last_slash - pe_path;
            if (dir_len >= sizeof(g_exe_dir)) dir_len = sizeof(g_exe_dir) - 1;
            __builtin_memcpy(g_exe_dir, pe_path, dir_len);
            g_exe_dir[dir_len] = '\0';
            return;
        }
    }
    /* Fallback to CWD — use "." since getcwd needs glibc */
    g_exe_dir[0] = '.';
    g_exe_dir[1] = '\0';
}

/**
 * Find the .text jmp-thunk address whose IAT entry resolves to target_addr.
 */
void *find_text_thunk(void *image_base, IMAGE_NT_HEADERS64 *nt,
                       IMAGE_SECTION_HEADER *sections,
                       void *target_addr)
{
    return find_rip_relative_jump_to(image_base, nt, sections,
                                      nt->FileHeader.NumberOfSections,
                                      target_addr);
}

static void *resolve_import(const char *dll_name, const char *func_name)
{
    /* Tier 1: lookup in our stub import table (hand-rolled binary search) */
    size_t lo = 0, hi = import_table_count;
    import_entry_t *entry = NULL;
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
    if (entry != NULL && entry->address != NULL) {
        if (entry->dll_name && dll_strcasecmp(entry->dll_name, dll_name) != 0) {
            DEBUG("  WARNING: %s found in %s but requested from %s",
                  func_name, entry->dll_name, dll_name);
        }
        return entry->address;
    }

    /* Tier 2: lookup in loaded module exports */
    loaded_module_t *mod = find_module_by_name(dll_name);
    if (mod != NULL && mod->export_cache.number_of_names > 0) {
        void *addr = lookup_export(mod, func_name);
        if (addr != NULL) {
            DEBUG("    Resolved %s!%s from module %s via export table -> %p",
                  dll_name, func_name, mod->name, addr);
            return addr;
        }
    }

    /* Tier 3: not found */
    DEBUG("  ERROR: unresolved import: %s!%s", dll_name, func_name);
    return NULL;
}

/**
 * Pass 1: resolve import names and write to the descriptor's FirstThunk (IAT).
 */
static int resolve_import_pass1(void *base, IMAGE_NT_HEADERS64 *nt)
{
    IMAGE_OPTIONAL_HEADER64 *opt = &nt->OptionalHeader;

    if (opt->DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT].Size == 0) {
        DEBUG("No imports to resolve");
        return 0;
    }

    uint64_t import_rva = opt->DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT].VirtualAddress;
    IMAGE_IMPORT_DESCRIPTOR *desc = (IMAGE_IMPORT_DESCRIPTOR *)((char *)base + import_rva);

    DEBUG("Resolving imports:");

    while (desc->Name != 0) {
        const char *dll_name = (const char *)((char *)base + desc->Name);

        DEBUG("  DLL: %s", dll_name);

        IMAGE_THUNK_DATA64 *orig_thunks = (IMAGE_THUNK_DATA64 *)((char *)base + desc->u1.OriginalFirstThunk);
        IMAGE_THUNK_DATA64 *iath = (IMAGE_THUNK_DATA64 *)((char *)base + desc->FirstThunk);

        int i;
        for (i = 0; orig_thunks[i].AddressOfData != 0; i++) {
            void *addr = NULL;
            const char *func_name_for_debug = NULL;

            if (orig_thunks[i].AddressOfData & 0x8000000000000000ULL) {
                /* Ordinal import (high bit set) */
                uint16_t ordinal = (uint16_t)(orig_thunks[i].AddressOfData & 0xFFFF);
                const char *func_name = ordinal_lookup(dll_name, ordinal);
                if (func_name != NULL) {
                    addr = resolve_import(dll_name, func_name);
                    DEBUG("    Resolved ordinal %s!%d -> %s -> %p",
                          dll_name, ordinal, func_name, addr);
                } else {
                    DEBUG("  WARNING: ordinal import %s!%d not in lookup table",
                          dll_name, ordinal);
                }
                func_name_for_debug = "<ordinal>";
            } else {
                /* Name import */
                IMAGE_IMPORT_BY_NAME *imp_name = (IMAGE_IMPORT_BY_NAME *)((char *)base + orig_thunks[i].AddressOfData);
                func_name_for_debug = (const char *)imp_name->Name;
                addr = resolve_import(dll_name, func_name_for_debug);
            }

            if (addr != NULL) {
                DEBUG("    Resolved %s -> %p", func_name_for_debug, addr);
                iath[i].AddressOfData = (uint64_t)(uintptr_t)addr;
            } else {
                DEBUG("    FAILED to resolve import at index %d", i);
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
static int collect_thunk_targets(void *base, IMAGE_NT_HEADERS64 *nt,
                                 uint64_t targets[MAX_THUNK_TARGETS])
{
    const IMAGE_DOS_HEADER *img_dos = (const IMAGE_DOS_HEADER *)base;
    uint32_t pe_off = img_dos->e_lfanew;
    uint32_t sec_off = pe_off + sizeof(uint32_t) + sizeof(IMAGE_FILE_HEADER) +
                       nt->FileHeader.SizeOfOptionalHeader;
    IMAGE_SECTION_HEADER *sections = (IMAGE_SECTION_HEADER *)((char *)base + sec_off);

    return scan_rip_relative_jumps(base, nt, sections,
                                    nt->FileHeader.NumberOfSections,
                                    targets, MAX_THUNK_TARGETS);
}

/**
 * Match thunk IAT targets against the flat import array and patch mismatches.
 */
static int patch_thunk_targets(void *base, IMAGE_NT_HEADERS64 *nt,
                               uint64_t *targets, int num_targets,
                               struct import_flat *flat, int num_flat)
{
    IMAGE_OPTIONAL_HEADER64 *opt = &nt->OptionalHeader;
    uint64_t import_dir_va = opt->DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT].VirtualAddress;
    uint64_t import_dir_end = opt->DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT].Size;

    int matched = 0;
    int t;
    for (t = 0; t < num_targets; t++) {
        uint64_t target = targets[t];
        uint64_t *target_ptr = (uint64_t *)((char *)base + target);
        uint64_t current_val = *target_ptr;

        int did_match = strategy_resolved_overlap(current_val, flat, num_flat) ||
            strategy_ilt_value_match(target_ptr, current_val, target, flat, num_flat) ||
            strategy_ilt_offset_match(target_ptr, target, current_val,
                                      import_dir_va, import_dir_end, flat, num_flat) ||
            strategy_positional(target_ptr, target, t, flat, num_flat);
        if (did_match) {
            matched++;
        }
    }

    return matched;
}

/**
 * Pass 2: patch thunk IAT targets found by scanning .text
 */
static int resolve_import_pass2(void *base, IMAGE_NT_HEADERS64 *nt)
{
    IMAGE_OPTIONAL_HEADER64 *opt = &nt->OptionalHeader;

    if (opt->DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT].Size == 0) {
        return 0;
    }

    uint64_t thunk_targets[MAX_THUNK_TARGETS];
    int num_targets = collect_thunk_targets(base, nt, thunk_targets);

    if (num_targets == 0)
        return 0;

    DEBUG("  Found %d thunk targets in .text (range 0x%lx-0x%lx)",
          num_targets,
          (unsigned long)thunk_targets[0],
          (unsigned long)thunk_targets[num_targets - 1] + 7);

    struct import_flat flat[MAX_FLAT_IMPORTS];
    int num_flat = build_flat_import_array(base, nt, flat);

    DEBUG("  Flat import array: %d entries", num_flat);

    int matched = patch_thunk_targets(base, nt, thunk_targets, num_targets, flat, num_flat);

    DEBUG("  Thunk IAT patched: %d/%d targets resolved", matched, num_targets);

    return 0;
}

/**
 * Resolve all imports in the PE image.
 */
int resolve_imports(void *base, IMAGE_NT_HEADERS64 *nt)
{
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
        DEBUG("  ERROR: import resolution depth exceeded (%d) for %s",
              MAX_IMPORT_DEPTH, mod->name);
        return -1;
    }

    void *base = mod->base;
    IMAGE_NT_HEADERS64 *nt = mod->nt;

    IMAGE_OPTIONAL_HEADER64 *opt = &nt->OptionalHeader;
    if (opt->DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT].Size == 0) {
        return 0; /* No imports */
    }

    uint64_t import_rva = opt->DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT].VirtualAddress;
    IMAGE_IMPORT_DESCRIPTOR *desc = (IMAGE_IMPORT_DESCRIPTOR *)((char *)base + import_rva);

    /* First pass: ensure all dependency DLLs are loaded */
    IMAGE_IMPORT_DESCRIPTOR *d = desc;
    while (d->Name != 0) {
        const char *dll_name = (const char *)((char *)base + d->Name);

        /* Check if already loaded */
        loaded_module_t *dep = find_module_by_name(dll_name);
        if (dep == NULL) {
            /* Check if this is a known stub library */
            if (strci_equal("kernel32.dll", dll_name) ||
                strci_equal("ntdll.dll", dll_name) ||
                strci_equal("msvcrt.dll", dll_name)) {
                DEBUG("  Skipping stub library '%s' for %s (resolved via import table)",
                      dll_name, mod->name);
                d++;
                continue;
            }

            /* Need to load this DLL */
            char path[512];
            if (!find_dll_path(dll_name, path, sizeof(path))) {
                DEBUG("  ERROR: cannot find DLL '%s' imported by %s",
                      dll_name, mod->name);
                return -1;
            }

            /* Load the DLL (map + relocate + register) */
            dep = load_dll(path, depth + 1);
            if (dep == NULL) {
                DEBUG("  ERROR: failed to load '%s' for %s",
                      dll_name, mod->name);
                return -1;
            }
        }
        d++;
    }

    /* Second pass: resolve all imports using the three-tier resolver */
    if (resolve_imports(base, nt) != 0) {
        DEBUG("  ERROR: import resolution failed for %s", mod->name);
        return -1;
    }

    /* Parse exports so this module's functions can be found by others */
    if (parse_export_table(mod) != 0) {
        /* parse_export_table returns -1 if no export dir — that's OK */
        DEBUG("  parse_export_table returned -1 for %s (no exports?)", mod->name);
    }

    return 0;
}

/* ───────────────────────────────────────────────────────────── */
/* Syscall-safe helpers for find_dll_path                      */
/* No glibc — suitable for WINE_STUB context without GS switch  */
/* ───────────────────────────────────────────────────────────── */

static void dll_copy_str(char *dst, const char *src, size_t len)
{
    size_t i;
    for (i = 0; i < len; i++)
        dst[i] = src[i];
}

static size_t dll_strlen(const char *s)
{
    size_t len = 0;
    while (s[len]) len++;
    return len;
}

static int dll_strncmp(const char *a, const char *b, size_t n)
{
    size_t i;
    for (i = 0; i < n; i++) {
        if (a[i] != b[i]) return (unsigned char)a[i] - (unsigned char)b[i];
        if (a[i] == '\0') return 0;
    }
    return 0;
}

static const char *dll_strchr(const char *s, int c)
{
    while (*s) {
        if (*s == (char)c) return s;
        s++;
    }
    return NULL;
}

static int dll_build_path(char *dst, size_t dst_size,
                          const char *dir, const char *name)
{
    size_t d_len = dll_strlen(dir);
    size_t n_len = dll_strlen(name);
    if (d_len + 1 + n_len + 1 > dst_size)
        return -1;
    dll_copy_str(dst, dir, d_len);
    dst[d_len] = '/';
    dll_copy_str(dst + d_len + 1, name, n_len);
    dst[d_len + 1 + n_len] = '\0';
    return 0;
}

static int dll_path_exists(const char *p)
{
    long fd = INLINE_SYSCALL_OPENAT(AT_FDCWD, p, O_RDONLY);
    if (fd >= 0) {
        INLINE_SYSCALL_CLOSE(fd);
        return 1;
    }
    return 0;
}

int find_dll_path(const char *dll_name, char *path, size_t path_size)
{
    /* --- Try current directory --- */
    if (dll_build_path(path, path_size, ".", dll_name) == 0) {
        if (dll_path_exists(path))
            return 1;
    }

    /* --- Try app directory --- */
    init_exe_dir();
    if (g_exe_dir[0] != '.' || g_exe_dir[1] != '\0') {
        if (dll_build_path(path, path_size, g_exe_dir, dll_name) == 0) {
            if (dll_path_exists(path))
                return 1;
        }
    }

    /* --- Try WINE_DLL_PATH (semicolon-separated) via environ --- */
    {
        const char *env_key = "WINE_DLL_PATH=";
        const size_t env_key_len = 14;  /* strlen("WINE_DLL_PATH=") */
        const char *env_val = NULL;

        for (char **ep = environ; *ep != NULL; ep++) {
            if (dll_strncmp(*ep, env_key, env_key_len) == 0) {
                env_val = *ep + env_key_len;
                break;
            }
        }
        if (env_val != NULL) {
            #define DLL_PATH_MAX_SEGMENTS 32
            char path_buf[1024];
            const char *segments[DLL_PATH_MAX_SEGMENTS];
            int seg_count = 0;

            size_t env_len = dll_strlen(env_val);
            if (env_len >= sizeof(path_buf)) env_len = sizeof(path_buf) - 1;
            dll_copy_str(path_buf, env_val, env_len);
            path_buf[env_len] = '\0';

            char *p = path_buf;
            while (seg_count < DLL_PATH_MAX_SEGMENTS && p != NULL) {
                const char *semi = dll_strchr(p, ';');
                if (semi != NULL) {
                    *(char *)semi = '\0';
                    segments[seg_count++] = p;
                    p = (char *)semi + 1;
                } else {
                    if (*p != '\0') {
                        segments[seg_count++] = p;
                    }
                    break;
                }
            }

            int i;
            for (i = 0; i < seg_count; i++) {
                if (dll_build_path(path, path_size, segments[i], dll_name) == 0) {
                    if (dll_path_exists(path))
                        return 1;
                }
            }
        }
    }

    return 0;
}

/* load_dll: map a DLL, apply relocations, register in module list + LDR,
 * resolve its imports. Returns the loaded_module_t or NULL on failure.
 *
 * Glibc-free: all string/memory ops are hand-rolled or __builtin.
 * Suitable for calling from WINE_STUB context on guest stack.
 */
loaded_module_t *load_dll(const char *path, int depth)
{
    /* Save main PE globals — map_image overwrites them with the DLL's values */
    void *saved_image_base = g_image_base;
    char saved_pe_path[512];
    const char *cur_pe_path = get_pe_path();
    if (cur_pe_path) {
        size_t pe_len = 0;
        while (cur_pe_path[pe_len] && pe_len < sizeof(saved_pe_path) - 1)
            pe_len++;
        __builtin_memcpy(saved_pe_path, cur_pe_path, pe_len);
        saved_pe_path[pe_len] = '\0';
    } else {
        saved_pe_path[0] = '\0';
    }

    /* Map the DLL */
    IMAGE_NT_HEADERS64 nt_copy;
    void *base = map_image(path, NULL, &nt_copy, NULL);

    /* Restore main PE globals */
    g_image_base = saved_image_base;
    set_pe_path(saved_pe_path);

    if (base == NULL) {
        DEBUG("  ERROR: map_image failed for '%s'", path);
        return NULL;
    }

    /* Extract NT headers from image memory */
    IMAGE_DOS_HEADER *img_dos = (IMAGE_DOS_HEADER *)base;
    IMAGE_NT_HEADERS64 *img_nt = (IMAGE_NT_HEADERS64 *)((char *)base + img_dos->e_lfanew);

    /* Extract DLL name from path (hand-rolled strrchr) */
    const char *name = path;
    const char *p = path;
    while (*p) {
        if (*p == '/') name = p + 1;
        p++;
    }

    /* Register in module list */
    loaded_module_t *mod = add_module(base, name, img_nt);
    if (mod == NULL) {
        DEBUG("  ERROR: module list full, cannot load '%s'", name);
        munmap(base, img_nt->OptionalHeader.SizeOfImage);
        return NULL;
    }

    /* Add to PEB LDR */
    if (g_peb_ldr != NULL) {
        ldr_add_module(mod);
    }

    /* Resolve this DLL's own imports (recursive).
     * resolve_module_imports also calls parse_export_table internally. */
    if (resolve_module_imports(mod, depth + 1) != 0) {
        DEBUG("  ERROR: import resolution failed for '%s'", name);
        /* Cleanup all resources allocated above */
        if (g_peb_ldr != NULL && mod->ldr_linked) {
            ldr_remove_module(mod);
        }
        reset_export_cache(mod);
        remove_module(mod);
        munmap(base, img_nt->OptionalHeader.SizeOfImage);
        return NULL;
    }

    /* Parse exports if not already done (e.g., DLL has no imports but has exports) */
    if (mod->export_cache.number_of_names == 0 &&
        img_nt->OptionalHeader.DataDirectory[DIRECTORY_ENTRY_EXPORT].VirtualAddress != 0) {
        parse_export_table(mod);
    }

    DEBUG("Loaded DLL: %s at %p", name, base);
    return mod;
}
