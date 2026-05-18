/*
 * main.c — PE32+ loader orchestrator (my_wine64)
 *
 * Opens a PE32+ binary, maps sections with correct protections,
 * resolves imports, sets up TEB/PEB, allocates a guest stack,
 * and jumps to the entry point.
 *
 * PE32 images are rejected — use my_wine wrapper or my_wine32 directly.
 *
 * All heavy lifting is delegated to src/loader/ sub-modules.
 */

#define _GNU_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/stat.h>

#include "include/pe.h"
#include "include/msvcrt.h"
#include "include/crt.h"
#include "include/common.h"
#include "loader/loader_priv.h"
#include "src/pe_priv.h"
#include "include/debug.h"

void patch_crt_refptrs(const char *file_path, void *image_base,
                       IMAGE_NT_HEADERS *nt, IMAGE_SECTION_HEADER *sections);

extern char **environ;

/* Scan char** envp for "KEY=..." and return the value (after '=')
 * or NULL if not found. */
static const char *envp_lookup(char *const envp[], const char *key)
{
    size_t key_len = strlen(key);
    for (int i = 0; envp[i] != NULL; i++) {
        if (strncmp(envp[i], key, key_len) == 0 && envp[i][key_len] == '=') {
            return envp[i] + key_len + 1;
        }
    }
    return NULL;
}

static void init_runtime_config(char *const envp[])
{
    const char *debug_level = envp_lookup(envp, "MY_WINE_DEBUG_LEVEL");
    g_debug_level = parse_debug_level(debug_level);

    set_wine_dll_path(envp_lookup(envp, "WINE_DLL_PATH"));
}

/* Pre-seed argc/argv/envp in .bss using COFF-derived offsets from g_crt.crt_ctx.
 * Explicit mprotect ensures .bss is writable.
 *
 * This is a fallback used when the active CRT module does not
 * provide a seed_bss vtable entry. g_crt.crt_ctx must be populated
 * by patch_crt_refptrs() (or the module's discover_offsets) first. */
static void seed_bss_vars(void *base,
                          const IMAGE_NT_HEADERS *nt,
                          IMAGE_SECTION_HEADER *sections)
{
    if (g_crt.crt_ctx.bss_vaddr == 0) {
        fprintf(stderr, "WARNING: .bss section not found, "
                "skipping argc/argv/envp pre-seed\n");
        return;
    }

    uint8_t *bss_base = (uint8_t *)base + g_crt.crt_ctx.bss_vaddr;

    const IMAGE_SECTION_HEADER *bss_sec = find_section_by_name(nt, sections, ".bss");
    if (bss_sec == NULL) {
        fprintf(stderr, "WARNING: .bss section not found in headers, "
                "skipping pre-seed\n");
        return;
    }

    size_t bss_size = bss_sec->Misc.VirtualSize;
    if (bss_size == 0) bss_size = bss_sec->SizeOfRawData;

    uintptr_t bss_page = (uintptr_t)bss_base & ~(uintptr_t)PAGE_MASK;
    if (mprotect((void *)bss_page, (bss_size + PAGE_MASK) & ~(size_t)PAGE_MASK,
                  PROT_READ | PROT_WRITE) != 0) {
        fprintf(stderr, "WARNING: mprotect .bss failed, skipping pre-seed\n");
        return;
    }

    if (g_crt.crt_ctx.argc_bss_offset != 0) {
        *(uint32_t *)(bss_base + g_crt.crt_ctx.argc_bss_offset) = 1;
        DEBUG_LEVEL(2, ".bss: wrote argc=1 at offset 0x%x", g_crt.crt_ctx.argc_bss_offset);
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
        DEBUG_LEVEL(2, ".bss: wrote argv=NULL at offset 0x%x", g_crt.crt_ctx.argv_bss_offset);
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
        DEBUG_LEVEL(2, ".bss: wrote envp=NULL at offset 0x%x", g_crt.crt_ctx.envp_bss_offset);
    } else {
        fprintf(stderr, "WARNING: envp_bss_offset is 0, "
                "skipping envp pre-seed\n");
    }
}

/**
 * Map the PE, detect PE32 vs PE32+, and either:
 *   - For PE32:  unmap, print error, return -1 (use my_wine32 instead).
 *   - For PE32+: full loader setup (imports, TEB, PEB, etc.),
 *                return PE_TYPE_64 with outputs filled.
 */
static int init_loader(int argc, char **argv,
                       uint64_t *out_entry,
                       void **out_base,
                       void **out_stack,
                       void **out_teb,
                       int *out_pe_type)
{
    if (argc < 2) {
        fprintf(stderr, "Usage: %s <pe_binary>\n", argv[0]);
        return -1;
    }

    /* Parse setup-time environment before GS can point at the guest TEB. */
    init_runtime_config(environ);

    IMAGE_DOS_HEADER dos;
    IMAGE_NT_HEADERS nt;
    size_t nt_size;
    void *base = map_image(argv[1], &dos, &nt, &nt_size);
    if (!base) return -1;

    crt_type_t crt_type = crt_detect_type(argv[1], &nt);
    const crt_module_t *mod = crt_get_module(crt_type);
    crt_set_active(mod);

    IMAGE_SECTION_HEADER *sections = get_image_sections(base, &nt);

    if (pe_is_pe32(&nt)) {
        if (munmap(base, pe_size_of_image(&nt)) != 0) {
            perror("WARNING: munmap on PE32 reject");
        }
        fprintf(stderr, "Error: PE32 binary detected. Use my_wine wrapper or my_wine32 directly.\n");
        return -1;
    }

    init_msvcrt_imports();
    init_import_table();

    patch_crt_refptrs(argv[1], base, &nt, sections);

    resolve_imports(base, &nt);

    void *teb = setup_teb_peb();
    if (!teb) return -1;

    void *stack_top = setup_stack(&nt);
    if (!stack_top) return -1;

    {
        const IMAGE_SECTION_HEADER *data_sec = find_section_by_name(&nt, sections, ".data");
        if (data_sec == NULL) {
            fprintf(stderr, "WARNING: .data section not found\n");
        } else {
            uint64_t data_vaddr = data_sec->VirtualAddress;
            /* image_mapper already copied SizeOfRawData bytes of initialized data.
             * Only zero the padding tail after the raw data (VirtualSize - SizeOfRawData). */
            if (data_sec->Misc.VirtualSize > data_sec->SizeOfRawData) {
                size_t padding_off = data_sec->SizeOfRawData;
                size_t padding_size = data_sec->Misc.VirtualSize - data_sec->SizeOfRawData;
                memset((uint8_t *)base + data_vaddr + padding_off, 0, padding_size);
                DEBUG_LEVEL(2, ".data section: vaddr=0x%lx, raw=0x%lx, virt=0x%lx, zeroed %lu padding bytes",
                       (unsigned long)data_vaddr, (unsigned long)data_sec->SizeOfRawData,
                       (unsigned long)data_sec->Misc.VirtualSize, (unsigned long)padding_size);
            } else {
                DEBUG_LEVEL(2, ".data section: vaddr=0x%lx, size=0x%lx, no padding to zero",
                       (unsigned long)data_vaddr, (unsigned long)data_sec->Misc.VirtualSize);
            }
        }
    }

    /*
     * Prefer CRT-specific BSS seeding. The fallback uses offsets discovered
     * during refptr patching from _argc/__argc, _argv/__argv, and _environ/__envp.
     */
    const crt_module_t *active = crt_get_active();
    if (crt_has_seed_bss(active)) {
        crt_seed_bss(active, base, &nt, sections);
    } else {
        seed_bss_vars(base, &nt, sections);
    }

    static char *guest_argv[2];
    guest_argv[0] = argv[1];  /* the PE path */
    guest_argv[1] = NULL;
    char **guest_envp = environ;

    g_crt.guest_argv = guest_argv;
    g_crt.guest_envp = guest_envp;

    /* Keep __getmainargs/GetCommandLineA pointed at the actual PE path. */
    strncpy(g_crt.cmdline_storage, argv[1], sizeof(g_crt.cmdline_storage) - 1);
    g_crt.cmdline_storage[sizeof(g_crt.cmdline_storage) - 1] = '\0';

    /* CRT modules may bypass startup by naming a user entry symbol. */
    uint64_t main_rva = 0;
    {
        IMAGE_SYMBOL *symbols = NULL;
        char *string_table = NULL;
        int sym_count = parse_symbol_table_from_file(argv[1], &nt, &symbols, &string_table);
        if (sym_count > 0) {
            const char *const *entry_syms = crt_entry_symbols(mod);
            if (entry_syms) {
                for (int si = 0; entry_syms[si] != NULL; si++) {
                    main_rva = lookup_symbol_rva(symbols, sym_count, string_table,
                                                  sections, pe_section_count(&nt),
                                                  entry_syms[si]);
                    if (main_rva != 0) break;
                }
            } else {
                /* No module or no entry symbols — try "main" as default */
                main_rva = lookup_symbol_rva(symbols, sym_count, string_table,
                                              sections, pe_section_count(&nt),
                                              "main");
            }
            free(symbols);
        }
    }

    uint64_t entry_abs;
    if (main_rva != 0) {
        entry_abs = (uint64_t)(uintptr_t)base + main_rva;
        DEBUG("Bypassing CRT: jumping to entry at 0x%lx instead of entry 0x%lx",
                (unsigned long)entry_abs,
                (unsigned long)((uint64_t)(uintptr_t)base + pe_entry_rva(&nt)));
    } else {
        entry_abs = (uint64_t)(uintptr_t)base + pe_entry_rva(&nt);
        fprintf(stderr, "WARNING: entry symbol not found, "
                "using entry point 0x%lx\n", (unsigned long)entry_abs);
    }

    *out_entry = entry_abs;
    *out_base = base;
    *out_stack = stack_top;
    *out_teb = teb;
    *out_pe_type = PE_TYPE_64;

    return 0;
}

int main(int argc, char *argv[])
{
    uint64_t entry;
    void *base, *stack_top, *teb;
    int pe_type;

    if (init_loader(argc, argv, &entry, &base, &stack_top, &teb, &pe_type) != 0) {
        return 1;
    }

    run_guest_entry(entry, base, stack_top, teb, g_crt.guest_argv, g_crt.guest_envp);
    return 0;
}
