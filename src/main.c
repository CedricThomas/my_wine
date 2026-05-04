/*
 * main.c — PE loader orchestrator
 *
 * Opens a PE32+ binary, maps sections with correct protections,
 * resolves imports, sets up TEB/PEB, allocates a guest stack,
 * and jumps to the entry point.
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
#include "loader/loader_priv.h"

extern char **environ;  // from libc, for guest envp

/* ── Helpers ─────────────────────────────────────────────────── */

/* Pre-seed argc/argv/envp in .bss using COFF-derived offsets
 * from g_crt_ctx. Explicit mprotect ensures .bss is writable. */
static void seed_bss_vars(void *base,
                          const IMAGE_NT_HEADERS64 *nt,
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
    uintptr_t bss_page = (uintptr_t)bss_base & ~(uintptr_t)4095;
    if (mprotect((void *)bss_page, (bss_size + 4095) & ~(size_t)4095,
                  PROT_READ | PROT_WRITE) != 0) {
        fprintf(stderr, "WARNING: mprotect .bss failed, skipping pre-seed\n");
        return;
    }

    if (g_crt_ctx.argc_bss_offset != 0) {
        *(uint32_t *)(bss_base + g_crt_ctx.argc_bss_offset) = 1;
        fprintf(stderr, ".bss: wrote argc=1 at offset 0x%x\n",
                g_crt_ctx.argc_bss_offset);
    } else {
        fprintf(stderr, "WARNING: argc_bss_offset is 0, "
                "skipping argc pre-seed\n");
    }

    if (g_crt_ctx.argv_bss_offset != 0) {
        *(uint64_t *)(bss_base + g_crt_ctx.argv_bss_offset) = 0;
        fprintf(stderr, ".bss: wrote argv=NULL at offset 0x%x\n",
                g_crt_ctx.argv_bss_offset);
    } else {
        fprintf(stderr, "WARNING: argv_bss_offset is 0, "
                "skipping argv pre-seed\n");
    }

    if (g_crt_ctx.envp_bss_offset != 0) {
        *(uint64_t *)(bss_base + g_crt_ctx.envp_bss_offset) = 0;
        fprintf(stderr, ".bss: wrote envp=NULL at offset 0x%x\n",
                g_crt_ctx.envp_bss_offset);
    } else {
        fprintf(stderr, "WARNING: envp_bss_offset is 0, "
                "skipping envp pre-seed\n");
    }
}

/* ── main ────────────────────────────────────────────────────── */

int main(int argc, char *argv[])
{
    if (argc < 2) {
        fprintf(stderr, "Usage: %s <pe_binary>\n", argv[0]);
        return 1;
    }

    /* 1. Map the PE image (open file, parse headers, copy sections, set protections) */
    IMAGE_DOS_HEADER dos;
    IMAGE_NT_HEADERS64 nt;
    size_t nt_size;
    void *base = map_image(argv[1], &dos, &nt, &nt_size);
    if (!base) return 1;

    /* 2. Get section headers (from the live image) */
    uint32_t pe_off = dos.e_lfanew;
    uint32_t sec_off = pe_off + sizeof(uint32_t) + sizeof(IMAGE_FILE_HEADER) +
                       nt.FileHeader.SizeOfOptionalHeader;
    IMAGE_SECTION_HEADER *sections = (IMAGE_SECTION_HEADER *)((char *)base + sec_off);

    /* 3. Initialize dynamic msvcrt import entries, then sort for bsearch */
    init_msvcrt_imports();
    init_import_table();

    /* 4. Patch CRT refptrs so the PE can find our global variables */
    patch_crt_refptrs(argv[1], base, &nt, sections);

    /* 5. Resolve imports */
    resolve_imports(base, &nt);

    /* 6. Set up TEB/PEB */
    void *teb = setup_teb_peb();
    if (!teb) return 1;

    /* 7. Set up stack */
    void *stack_top = setup_stack(&nt.OptionalHeader);
    if (!stack_top) return 1;

    /* 8a. Zero .data section */
    {
        IMAGE_SECTION_HEADER *data_sec = find_section_by_name(&nt, sections, ".data");
        if (data_sec == NULL) {
            fprintf(stderr, "WARNING: .data section not found\n");
        } else {
            uint64_t data_vaddr = data_sec->VirtualAddress;
            size_t data_size = data_sec->Misc.VirtualSize;
            if (data_size == 0) {
                data_size = data_sec->SizeOfRawData;
            }
            /* Zero the entire .data section */
            memset((uint8_t *)base + data_vaddr, 0, data_size);

            printf(".data section: vaddr=0x%lx, size=0x%lx, zeroed\n",
                   (unsigned long)data_vaddr, (unsigned long)data_size);
        }
    }

    /* 8b. Pre-seed argc/argv/envp in .bss
     *
     * g_crt_ctx was populated by patch_crt_refptrs (step 4) which
     * looks up _argc/__argc, _argv/__argv, _environ/__envp in the
     * COFF symbol table and computes offsets relative to .bss base.
     * Fallback to hardcoded values if COFF lookup was incomplete.
     */
    seed_bss_vars(base, &nt, sections);

    /* 9. Build guest argv/envp from actual host arguments */
    char *guest_argv[2];
    guest_argv[0] = argv[1];  /* the PE path */
    guest_argv[1] = NULL;
    char **guest_envp = environ;  /* real host environment */

    /* Set msvcrt globals so __getmainargs can return the real values */
    g_guest_argv = guest_argv;
    g_guest_envp = guest_envp;

    /* Fill _cmdline_storage so _acmdln points to the actual PE path */
    strncpy(_cmdline_storage, argv[1], sizeof(_cmdline_storage) - 1);
    _cmdline_storage[sizeof(_cmdline_storage) - 1] = '\0';

    /* 10. Look up user main() symbol; fall back to entry point if not found */
    uint64_t main_rva = 0;
    {
        IMAGE_SYMBOL *symbols = NULL;
        char *string_table = NULL;
        int sym_count = parse_symbol_table_from_file(argv[1], &nt, &symbols, &string_table);
        if (sym_count > 0) {
            main_rva = lookup_symbol_rva(symbols, sym_count, string_table,
                                          sections, nt.FileHeader.NumberOfSections,
                                          "main");
            free(symbols);  // free the malloc'd buffer
        }
    }

    uint64_t entry_abs;
    if (main_rva != 0) {
        entry_abs = (uint64_t)(uintptr_t)base + main_rva;
        fprintf(stderr, "Bypassing CRT: jumping to main() at 0x%lx instead of entry 0x%lx\n",
                (unsigned long)entry_abs,
                (unsigned long)((uint64_t)(uintptr_t)base + nt.OptionalHeader.AddressOfEntryPoint));
    } else {
        entry_abs = (uint64_t)(uintptr_t)base + nt.OptionalHeader.AddressOfEntryPoint;
        fprintf(stderr, "WARNING: 'main' symbol not found, using entry point 0x%lx\n",
                (unsigned long)entry_abs);
    }

    return jump_to_entry(entry_abs, stack_top, g_stack_base, teb, guest_argv, guest_envp);
}
