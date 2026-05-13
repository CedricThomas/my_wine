/*
 * main.c — PE loader orchestrator
 *
 * Opens a PE/PE32+ binary, maps sections with correct protections,
 * resolves imports, sets up TEB/PEB, allocates a guest stack,
 * and jumps to the entry point.
 *
 * For PE32 images: forks a 32-bit child (my_wine_32) that
 * independently loads the image and runs it.
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
#include <sys/wait.h>

#include "include/pe.h"
#include "include/msvcrt.h"
#include "include/crt.h"
#include "include/common.h"
#include "loader/loader_priv.h"
#include "include/pe_priv.h"
#include "include/debug.h"

/* Forward declarations from msvcrt/crt_globals.c and crt_refptrs.c */
extern crt_context_t g_crt_ctx;
void patch_crt_refptrs(const char *file_path, void *image_base,
                       IMAGE_NT_HEADERS *nt, IMAGE_SECTION_HEADER *sections);

extern char **environ;  // from libc, for guest envp

/* ── Helpers ─────────────────────────────────────────────────── */

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

/* Pre-seed argc/argv/envp in .bss using COFF-derived offsets
 * from g_crt_ctx. Explicit mprotect ensures .bss is writable.
 *
 * This is a fallback used when the active CRT module does not
 * provide a seed_bss vtable entry. g_crt_ctx must be populated
 * by patch_crt_refptrs() (or the module's discover_offsets) first. */
static void seed_bss_vars(void *base,
                          const IMAGE_NT_HEADERS *nt,
                          IMAGE_SECTION_HEADER *sections)
{
    if (g_crt_ctx.bss_vaddr == 0) {
        fprintf(stderr, "WARNING: .bss section not found, "
                "skipping argc/argv/envp pre-seed\n");
        return;
    }

    uint8_t *bss_base = (uint8_t *)base + g_crt_ctx.bss_vaddr;

    IMAGE_SECTION_HEADER *bss_sec = find_section_by_name(nt, sections, ".bss");
    if (bss_sec == NULL) {
        fprintf(stderr, "WARNING: .bss section not found in headers, "
                "skipping pre-seed\n");
        return;
    }

    size_t bss_size = bss_sec->Misc.VirtualSize;
    if (bss_size == 0) bss_size = bss_sec->SizeOfRawData;

    /* Ensure .bss page is writable */
    uintptr_t bss_page = (uintptr_t)bss_base & ~(uintptr_t)PAGE_MASK;
    if (mprotect((void *)bss_page, (bss_size + PAGE_MASK) & ~(size_t)PAGE_MASK,
                  PROT_READ | PROT_WRITE) != 0) {
        fprintf(stderr, "WARNING: mprotect .bss failed, skipping pre-seed\n");
        return;
    }

    if (g_crt_ctx.argc_bss_offset != 0) {
        *(uint32_t *)(bss_base + g_crt_ctx.argc_bss_offset) = 1;
        DEBUG(".bss: wrote argc=1 at offset 0x%x", g_crt_ctx.argc_bss_offset);
    } else {
        fprintf(stderr, "WARNING: argc_bss_offset is 0, "
                "skipping argc pre-seed\n");
    }

    if (g_crt_ctx.argv_bss_offset != 0) {
        if (pe_is_pe32(nt)) {
            *(uint32_t *)(bss_base + g_crt_ctx.argv_bss_offset) = 0;
        } else {
            *(uint64_t *)(bss_base + g_crt_ctx.argv_bss_offset) = 0;
        }
        DEBUG(".bss: wrote argv=NULL at offset 0x%x", g_crt_ctx.argv_bss_offset);
    } else {
        fprintf(stderr, "WARNING: argv_bss_offset is 0, "
                "skipping argv pre-seed\n");
    }

    if (g_crt_ctx.envp_bss_offset != 0) {
        if (pe_is_pe32(nt)) {
            *(uint32_t *)(bss_base + g_crt_ctx.envp_bss_offset) = 0;
        } else {
            *(uint64_t *)(bss_base + g_crt_ctx.envp_bss_offset) = 0;
        }
        DEBUG(".bss: wrote envp=NULL at offset 0x%x", g_crt_ctx.envp_bss_offset);
    } else {
        fprintf(stderr, "WARNING: envp_bss_offset is 0, "
                "skipping envp pre-seed\n");
    }
}

/* ── init_loader ─────────────────────────────────────────────── */

/**
 * Map the PE, detect PE32 vs PE32+, and either:
 *   - For PE32:  map+parse headers only, unmap, return PE_TYPE_32.
 *                Caller will fork+exec my_wine_32.
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

    /* 0. Parse MY_WINE_DEBUG from environ; set global debug flag before GS switch */
    if (envp_lookup(environ, "MY_WINE_DEBUG") != NULL) {
        g_debug_enabled = 1;
    }

    /* 0b. Cache WINE_DLL_PATH before GS switch so find_dll_path is syscall-safe */
    {
        const char *dll_path = envp_lookup(environ, "WINE_DLL_PATH");
        if (dll_path != NULL) {
            strncpy(g_wine_dll_path, dll_path, sizeof(g_wine_dll_path) - 1);
            g_wine_dll_path[sizeof(g_wine_dll_path) - 1] = '\0';
        }
    }

    /* 1. Map the PE image (open file, parse headers, copy sections, set protections) */
    IMAGE_DOS_HEADER dos;
    IMAGE_NT_HEADERS nt;
    size_t nt_size;
    void *base = map_image(argv[1], &dos, &nt, &nt_size);
    if (!base) return -1;

    /* 1b. Detect CRT type and select the active CRT module */
    crt_type_t crt_type = crt_detect_type(argv[1], &nt);
    const crt_module_t *mod = crt_get_module(crt_type);

    /* 2. Get section headers (from the live image) */
    IMAGE_SECTION_HEADER *sections = get_image_sections(base, &nt);

    /* ── PE32 fast-path: unmap and let child handle it ─────── */
    if (pe_is_pe32(&nt)) {
        DEBUG("my_wine: PE32 detected, will fork my_wine_32 child");
        /* Unmap the image — child will remap from the file */
        if (munmap(base, (size_t)nt_size) != 0) {
            perror("WARNING: munmap before fork");
        }
        *out_pe_type = PE_TYPE_32;
        return 0;
    }

    /* ── PE32+ path: full loader setup ─────────────────────── */

    /* 3. Initialize dynamic msvcrt import entries, then sort for bsearch */
    init_msvcrt_imports();
    init_import_table();

    /* 4. Patch CRT refptrs so the PE can find our global variables */
    patch_crt_refptrs(argv[1], base, &nt, sections);

    /* 5. Resolve imports */
    resolve_imports(base, &nt);

    /* 6. Set up TEB/PEB */
    void *teb = setup_teb_peb();
    if (!teb) return -1;

    /* 7. Set up stack */
    void *stack_top = setup_stack(&nt);
    if (!stack_top) return -1;

    /* 8a. Zero .data section */
    {
        IMAGE_SECTION_HEADER *data_sec = find_section_by_name(&nt, sections, ".data");
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
                DEBUG(".data section: vaddr=0x%lx, raw=0x%lx, virt=0x%lx, zeroed %lu padding bytes",
                       (unsigned long)data_vaddr, (unsigned long)data_sec->SizeOfRawData,
                       (unsigned long)data_sec->Misc.VirtualSize, (unsigned long)padding_size);
            } else {
                DEBUG(".data section: vaddr=0x%lx, size=0x%lx, no padding to zero",
                       (unsigned long)data_vaddr, (unsigned long)data_sec->Misc.VirtualSize);
            }

        }
    }

    /* 8b. Pre-seed argc/argv/envp in .bss
     *
     * If the active CRT module provides a seed_bss vtable entry, use it
     * via the accessor function. Then always fall back to the local
     * seed_bss_vars() for safety — both write the same values (argc=1,
     * argv=NULL, envp=NULL) so double-seeding is harmless.
     *
     * g_crt_ctx is populated by patch_crt_refptrs (step 4) via the module's
     * discover_offsets, which looks up _argc/__argc, _argv/__argv,
     * _environ/__envp in the COFF symbol table and computes offsets
     * relative to .bss base.
     */
    if (mod) {
        crt_seed_bss(mod, base, &nt, sections);
    }
    seed_bss_vars(base, &nt, sections);

    /* 9. Build guest argv/envp from actual host arguments */
    static char *guest_argv[2];
    guest_argv[0] = argv[1];  /* the PE path */
    guest_argv[1] = NULL;
    char **guest_envp = environ;  /* real host environment */

    /* Set msvcrt globals so __getmainargs can return the real values */
    g_guest_argv = guest_argv;
    g_guest_envp = guest_envp;

    /* Fill _cmdline_storage so _acmdln points to the actual PE path
     * For PE32, GetCommandLineA() returns _acmdln which points here */
    strncpy(_cmdline_storage, argv[1], sizeof(_cmdline_storage) - 1);
    _cmdline_storage[sizeof(_cmdline_storage) - 1] = '\0';

    /* 10. Look up user entry symbol
     *  - Use crt_entry_symbols() from the active CRT module to iterate
     *    entry symbol candidates
     *  - Fall back to PE entry point if no symbol found
     */
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
            free(symbols);  // free the malloc'd buffer
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

    /* Write outputs for caller */
    *out_entry = entry_abs;
    *out_base = base;
    *out_stack = stack_top;
    *out_teb = teb;
    *out_pe_type = PE_TYPE_64;

    return 0;
}

/* ── main ────────────────────────────────────────────────────── */

int main(int argc, char *argv[])
{
    uint64_t entry;
    void *base, *stack_top, *teb;
    int pe_type;

    if (init_loader(argc, argv, &entry, &base, &stack_top, &teb, &pe_type) != 0) {
        return 1;
    }

    if (pe_type == PE_TYPE_32) {
        /* ── PE32: fork + exec my_wine_32 ─────────────────── */
        pid_t pid = fork();
        if (pid < 0) {
            perror("fork");
            return 1;
        }

        if (pid == 0) {
            /* Child: set env, exec my_wine_32 */
            char env_var[4096];
            snprintf(env_var, sizeof(env_var), "WINE32_PE_PATH=%s", argv[1]);
            putenv(env_var);

            /* Find my_wine_32 next to this binary via /proc/self/exe */
            char exe_path[4096];
            ssize_t len = readlink("/proc/self/exe", exe_path, sizeof(exe_path) - 1);
            char *exec_target = "my_wine_32";
            if (len > 0) {
                exe_path[len] = '\0';
                char *slash = strrchr(exe_path, '/');
                if (slash) {
                    strcpy(slash + 1, "my_wine_32");
                    exec_target = exe_path;
                }
            }

            char *child_argv[] = { exec_target, argv[1], NULL };
            execvp(exec_target, child_argv);

            /* execvp only returns on failure */
            perror("execvp my_wine_32");
            _exit(127);
        }

        /* Parent: wait for child */
        int status;
        if (waitpid(pid, &status, 0) < 0) {
            perror("waitpid");
            return 1;
        }

        if (WIFEXITED(status)) {
            return WEXITSTATUS(status);
        }
        if (WIFSIGNALED(status)) {
            return 128 + WTERMSIG(status);
        }
        return 1;
    }

    /* ── PE32+: single-process flow ───────────────────────── */
    run_guest_entry(entry, base, stack_top, teb, g_guest_argv, g_guest_envp);
    return 0;
}
