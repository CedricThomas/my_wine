/*
 * pe32_process.c -- PE32 process parameters, argv/envp, and CRT .bss setup.
 */

#include <stdint.h>
#include <stddef.h>
#include <string.h>

#include "../syscall/syscalls_inline.h"
#include "include/pe.h"
#include "include/pe_parser.h"
#include "include/common.h"
#include "src/pe_priv.h"
#include "include/syscall_safe_utils.h"
#include "teb_peb.h"
#include "peb_ldr.h"
#include "module_list.h"
#include "loader_state.h"
#include "../heap/wine_heap.h"
#include "pe32_process.h"

/*
 * Local BSS offset defines for MinGW CRT layout.
 *
 * The standalone 32-bit binary cannot load CRT modules, so it uses hardcoded
 * offsets matching the MinGW CRT .bss layout.
 */
#define CRT_BSS_INITENV   0x018   /* __initenv / _environ pointer */
#define CRT_BSS_ARGV      0x020   /* _argv pointer */
#define CRT_BSS_ARGC      0x028   /* _argc */
#define CRT_BSS_ACMDLN    0x030   /* _acmdln pointer (for GetCommandLineA) */

static uint32_t g_argv_ptr = 0;
void *g_argv_page = NULL;

extern void *g_process_heap;

void ensure_argv_setup(const char *pe_path)
{
    if (g_argv_ptr != 0)
        return;

    void *page = INLINE_SYSCALL_MMAP(NULL, PAGE_SIZE,
        PROT_READ | PROT_WRITE,
        MAP_PRIVATE | MAP_ANONYMOUS | MAP_32BIT, -1, 0);
    if (page == MAP_FAILED || page == NULL) {
        const char err[] = "my_wine32: failed to alloc 32-bit argv page\n";
        INLINE_SYSCALL_WRITE_ERR(err, sizeof(err) - 1);
        INLINE_SYSCALL_EXIT_GROUP(1);
    }
    g_argv_page = page;

    uint8_t *p = (uint8_t *)page;
    memset(page, 0, PAGE_SIZE);

    char *path_copy = (char *)(p + 0);
    syscall_safe_copy_str(path_copy, pe_path, 511);
    path_copy[510] = '\0';

    uint32_t *argv = (uint32_t *)(p + 0x200);
    argv[0] = (uint32_t)(uintptr_t)path_copy;
    argv[1] = 0;

    uint32_t *envp = (uint32_t *)(p + 0x208);
    {
        extern char **environ;
        envp[0] = (uint32_t)(uintptr_t)environ;
    }
    envp[1] = 0;

    g_argv_ptr = (uint32_t)(uintptr_t)argv;
}

uint32_t pe32_argv_ptr(void)
{
    return g_argv_ptr;
}

uint32_t pe32_envp_ptr(void)
{
    if (g_argv_page == NULL)
        return 0;
    return (uint32_t)(uintptr_t)((uint8_t *)g_argv_page + 0x208);
}

static void wire_peb32_heap(void *peb)
{
    uint8_t *p = (uint8_t *)peb;

    if (g_process_heap == NULL) {
        g_process_heap = init_process_heap();
    }
    if (g_process_heap) {
        uint32_t heap_val = (uint32_t)(uintptr_t)g_process_heap;
        *(uint32_t *)(p + 0x18) = heap_val;
        *(uint32_t *)(p + 0x3C) = heap_val;
    }
}

static void wire_peb32_params(void *peb, const char *pe_path)
{
    uint8_t *p = (uint8_t *)peb;

    void *params = INLINE_SYSCALL_MMAP(NULL, PAGE_SIZE,
        PROT_READ | PROT_WRITE,
        MAP_PRIVATE | MAP_ANONYMOUS | MAP_32BIT, -1, 0);
    if (params == MAP_FAILED || params == NULL) {
        return;
    }

    uint8_t *q = (uint8_t *)params;
    memset(params, 0, PAGE_SIZE);

    *(uint32_t *)(q + 0x00) = 0x200;
    *(uint32_t *)(q + 0x04) = 0x0E8;

    {
        uint16_t *cur_str = (uint16_t *)(q + 0x60);
        cur_str[0] = 'C'; cur_str[1] = ':'; cur_str[2] = '\\'; cur_str[3] = 0;

        *(uint16_t *)(q + 0x50) = 6;
        *(uint16_t *)(q + 0x52) = 8;
        *(uint32_t *)(q + 0x54) = (uint32_t)(uintptr_t)cur_str;
        *(uint32_t *)(q + 0x14) = (uint32_t)(uintptr_t)(q + 0x50);
    }

    {
        uint16_t *dll_str = (uint16_t *)(q + 0x60);

        *(uint16_t *)(q + 0x68) = 6;
        *(uint16_t *)(q + 0x6A) = 8;
        *(uint32_t *)(q + 0x6C) = (uint32_t)(uintptr_t)dll_str;
        *(uint32_t *)(q + 0x18) = (uint32_t)(uintptr_t)(q + 0x68);
    }

    {
        uint16_t *cmd_buf = (uint16_t *)(q + 0x80);
        size_t cmd_len = 0;
        const char *s = pe_path;
        while (*s && cmd_len < 255) {
            cmd_buf[cmd_len] = (uint16_t)(uint8_t)*s;
            s++;
            cmd_len++;
        }
        cmd_buf[cmd_len] = 0;

        *(uint16_t *)(q + 0x74) = (uint16_t)(cmd_len * 2);
        *(uint16_t *)(q + 0x76) = (uint16_t)((cmd_len + 1) * 2);
        *(uint32_t *)(q + 0x78) = (uint32_t)(uintptr_t)cmd_buf;
        *(uint32_t *)(q + 0x1C) = (uint32_t)(uintptr_t)(q + 0x74);
    }

    *(uint32_t *)(p + 0x10) = (uint32_t)(uintptr_t)params;
}

static void wire_peb32_ldr(void *peb, void *image_base, IMAGE_NT_HEADERS *nt)
{
    uint8_t *p = (uint8_t *)peb;

    if (loader_get_peb_ldr() == NULL) {
        init_module_list();
        loader_set_peb_ldr(init_peb_ldr());
    }
    if (loader_get_peb_ldr()) {
        *(uint32_t *)(p + 0x0C) = (uint32_t)(uintptr_t)loader_get_peb_ldr();

        int mod_idx = -1;
        for (int i = 0; i < g_loader.module_count && i < MAX_MODULES; i++) {
            if (g_loader.modules[i].base == image_base) {
                mod_idx = i;
                break;
            }
        }
        if (mod_idx < 0) {
            loaded_module_t *mod = add_module(image_base, "main.exe", nt);
            if (mod) {
                ldr_add_module(mod);
            }
        } else if (!g_loader.modules[mod_idx].ldr_linked) {
            ldr_add_module(&g_loader.modules[mod_idx]);
        }
    }
}

static void wire_peb32_os_version(void *peb)
{
    uint8_t *p = (uint8_t *)peb;

    *(uint16_t *)(p + 0x2E) = 0x0A;
    *(uint16_t *)(p + 0x30) = 0x00;
    *(uint16_t *)(p + 0x34) = 0x4A11;
}

void wire_peb32_fields(void *peb, void *image_base,
                       IMAGE_NT_HEADERS *nt, const char *pe_path)
{
    wire_peb32_heap(peb);
    wire_peb32_params(peb, pe_path);
    wire_peb32_ldr(peb, image_base, nt);
    wire_peb32_os_version(peb);
}

void seed_pe32_bss_vars(void *base, IMAGE_NT_HEADERS *nt)
{
    IMAGE_SECTION_HEADER *sections = get_image_sections(base, nt);
    const IMAGE_SECTION_HEADER *bss_sec = find_section_by_name(nt, sections, ".bss");
    if (bss_sec == NULL) {
        return;
    }

    size_t bss_size = bss_sec->Misc.VirtualSize;
    if (bss_size == 0) bss_size = bss_sec->SizeOfRawData;
    if (bss_size == 0) return;

    uint8_t *bss_base = pe_rva_to_ptr(base, nt, bss_sec->VirtualAddress,
                                      bss_size);
    if (bss_base == NULL) return;

    uintptr_t bss_page = (uintptr_t)bss_base & ~(uintptr_t)PAGE_MASK;
    size_t bss_pages = ((bss_size + PAGE_MASK) & ~(size_t)PAGE_MASK);
    if (bss_pages == 0) bss_pages = PAGE_SIZE;
    long mprot_rc = INLINE_SYSCALL_MPROTECT((void *)bss_page, bss_pages,
                                             PROT_READ | PROT_WRITE);
    if (mprot_rc != 0) {
        return;
    }

    if (CRT_BSS_ARGC < bss_size) {
        *(uint32_t *)(bss_base + CRT_BSS_ARGC) = 1;
    }

    if (CRT_BSS_ARGV < bss_size && g_argv_ptr != 0) {
        *(uint32_t *)(bss_base + CRT_BSS_ARGV) = g_argv_ptr;
    }

    if (CRT_BSS_INITENV < bss_size && g_argv_page != NULL) {
        *(uint32_t *)(bss_base + CRT_BSS_INITENV) = pe32_envp_ptr();
    }

    if (CRT_BSS_ACMDLN + 4 <= bss_size && g_argv_page != NULL) {
        uint32_t path_ptr = (uint32_t)(uintptr_t)((uint8_t *)g_argv_page + 0);
        *(uint32_t *)(bss_base + CRT_BSS_ACMDLN) = path_ptr;
    }
}
