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

    /* 8. Zero .data section and initialize global variables
     *
     * The memset below zeros the entire .data section, so the zero-init
     * values (has_cctor=0, managedapp=0, startinfo=NULL, mainret=0, envp=NULL,
     * argv=NULL) are redundant — they are already zero after memset.
     *
     * The offsets below (0x004, 0x008, 0x010, 0x018, 0x020, 0x028) are
     * relative to the .data section base. They are CRT-specific variable
     * offsets that ideally would come from the PE's symbol table, but are
     * linker-defined for mingw-w64 CRT startup layout.
     */
    {
        int data_section_idx = -1;
        for (int i = 0; i < nt.FileHeader.NumberOfSections; i++) {
            if (memcmp(sections[i].Name, ".data", 5) == 0) {
                data_section_idx = i;
                break;
            }
        }

        if (data_section_idx < 0) {
            fprintf(stderr, "WARNING: .data section not found\n");
        } else {
            IMAGE_SECTION_HEADER *data_sec = &sections[data_section_idx];
            uint64_t data_vaddr = data_sec->VirtualAddress;
            size_t data_size = data_sec->Misc.VirtualSize;
            if (data_size == 0) {
                data_size = data_sec->SizeOfRawData;
            }
            /* Zero the entire .data section */
            memset((uint8_t *)base + data_vaddr, 0, data_size);

            /* Compute base within .data section dynamically */
            uint8_t *data_base = (uint8_t *)base + data_vaddr;

            /* argc = 1, relative to .data section base */
            *(uint32_t *)(data_base + 0x028) = 1;

            printf(".data section: vaddr=0x%lx, size=0x%lx, initialized argc\n",
                   (unsigned long)data_vaddr, (unsigned long)data_size);
        }
    }

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

    /* Pre-seed argv/envp pointers in the PE's .bss with NULL so the CRT
     * startup doesn't crash dereferencing host addresses.
     * The real values will be set by __getmainargs via our stub.
     *
     * Use .bss section VA dynamically (set by patch_crt_refptrs in g_crt_ctx.bss_vaddr).
     * The offsets (0x018, 0x020) are relative to .bss base and are CRT-specific;
     * they correspond to the mingw-w64 CRT's envp/argv locations. */
    {
        if (g_crt_ctx.bss_vaddr != 0) {
            /* Skip .bss write for now - causes SIGSEGV on some systems
             * where the .bss page isn't properly writable after mprotect. */
            fprintf(stderr, "Skipping .bss pre-seed (bss_vaddr=0x%lx)\n",
                    (unsigned long)g_crt_ctx.bss_vaddr);
        } else {
            fprintf(stderr, "WARNING: g_crt_ctx.bss_vaddr not set, skipping .bss pre-seed\n");
        }
    }

    /* 10. Jump to entry point (pass absolute address, not RVA) */
    uint64_t entry_abs = (uint64_t)(uintptr_t)base + nt.OptionalHeader.AddressOfEntryPoint;
    return jump_to_entry(entry_abs, stack_top, g_stack_base, teb, guest_argv, guest_envp);
}
