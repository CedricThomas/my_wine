/*
 * dll_loader.c — DLL loading (map, relocate, register)
 *
 * Maps a DLL at a reserved base below 4GB, applies relocations,
 * registers in module list + LDR, and resolves its imports.
 *
 * Glibc-free: string/memory/debug helpers use syscall_safe_* utilities.
 * No PLT calls — safe to call from WINE_STUB context after GS→TEB switch.
 *
 * Extracted from import_resolve.c.
 */

#include "include/pe.h"
#include "include/pe_parser.h"
#include "src/pe_priv.h"
#include "include/common.h"
#include "include/nt_constants.h"
#include "include/wine_abi.h"
#include "loader_priv.h"
#include "export_table.h"
#include "module_list.h"
#include "peb_ldr.h"
#include "../syscall/syscalls_inline.h"
#include "include/syscall_safe_utils.h"
#include "image_mapper.h"
#include "dll_path.h"
#include "dll_loader.h"

#define DLL_PROCESS_DETACH 0
#define DLL_PROCESS_ATTACH 1

typedef int (KERNEL32_ABI *dll_entry_fn_t)(void *, uint32_t, void *);

static int invoke_module_dllmain(dll_entry_fn_t entry, void *base, uint32_t reason)
{
#if defined(__i386__)
    int result;

    __asm__ volatile(
        "pushf\n\t"
        "push %%ebx\n\t"
        "push %%esi\n\t"
        "push %%edi\n\t"
        "push %%ebp\n\t"
        "push $0\n\t"
        "push %[reason]\n\t"
        "push %[base]\n\t"
        "call *%[entry]\n\t"
        "pop %%ebp\n\t"
        "pop %%edi\n\t"
        "pop %%esi\n\t"
        "pop %%ebx\n\t"
        "popf\n\t"
        "cld\n\t"
        : "=a"(result)
        : [entry] "r"(entry), [base] "r"(base), [reason] "r"(reason)
        : "ecx", "edx", "memory", "cc");

    return result;
#else
    return entry(base, reason, NULL);
#endif
}

static int call_module_dllmain(loaded_module_t *mod, uint32_t reason)
{
    uint32_t entry_rva;
    dll_entry_fn_t entry;

    if (mod == NULL || mod->base == NULL || mod->nt == NULL)
        return 1;

    entry_rva = pe_entry_rva(mod->nt);
    if (entry_rva == 0)
        return 1;

    entry = (dll_entry_fn_t)((uint8_t *)mod->base + entry_rva);
    return invoke_module_dllmain(entry, mod->base, reason) != 0;
}

/* DLL base allocator: maps DLLs below 4GB to avoid GCC ms_abi truncation bug.
 * Uses atomic operations for allocation — still not fully thread-safe (mmap
 * and module registration are separate steps), but prevents overlapping bases.
 * The base tracker lives in g_loader.dll_base_next (volatile, atomic CAS).
 */

/**
 * load_dll: map a DLL, apply relocations, register in module list + LDR,
 * resolve its imports. Returns the loaded_module_t or NULL on failure.
 *
 * Glibc-free: string/memory ops use syscall_safe_* helpers.
 * Suitable for calling from WINE_STUB context on guest stack. */
loaded_module_t *load_dll(const char *path, int depth)
{
    if (g_debug_level >= 2) {
        const char msg[] = "load_dll: ENTER\n";
        INLINE_SYSCALL_WRITE(2, msg, sizeof(msg) - 1);
    }

    /* Save main PE globals — map_image_at overwrites them with the DLL's values */
    void *saved_image_base = g_loader.image_base;
    int saved_is_32bit = g_loader.is_32bit;
    char saved_pe_path[512];
    syscall_safe_copy_str(saved_pe_path, g_loader.pe_path, sizeof(saved_pe_path));

    /* Atomically reserve a page-aligned base for this DLL using CAS loop.
     * This prevents two threads from mapping at the same address.
     * We reserve at least PAGE_SIZE upfront; the rest is advanced after mapping. */
    uintptr_t alloc_base;
    do {
        uintptr_t expected = __atomic_load_n(&g_loader.dll_base_next, __ATOMIC_SEQ_CST);
        uintptr_t rounded = (expected + (PAGE_SIZE - 1)) & ~(uintptr_t)(PAGE_SIZE - 1);
        uintptr_t desired = rounded + PAGE_SIZE;  /* reserve minimum one page */
        if (__atomic_compare_exchange_n(&g_loader.dll_base_next, &expected, desired,
                                        false, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST)) {
            alloc_base = rounded;
            break;
        }
    } while (1);

    /* Map the DLL at the reserved base below 4GB to avoid GCC ms_abi truncation */
    IMAGE_NT_HEADERS nt_copy;
    void *base = map_image_at(path, NULL, &nt_copy, NULL, alloc_base);

    /* Restore main PE globals (regardless of success/failure) */
    g_loader.image_base = saved_image_base;
    g_loader.is_32bit = saved_is_32bit;
    syscall_safe_copy_str(g_loader.pe_path, saved_pe_path, sizeof(g_loader.pe_path));

    syscall_safe_debug_write_ptr(2, "load_dll: map=", base ? (uintptr_t)base : 0);
    if (base == NULL) {
        return NULL;
    }

    /* Advance g_loader.dll_base_next past the actual DLL size.
     * We already reserved PAGE_SIZE atomically above, so only add the remainder. */
    uintptr_t dll_size = (pe_size_of_image(&nt_copy) + (PAGE_SIZE - 1)) & ~(uintptr_t)(PAGE_SIZE - 1);
    if (dll_size > PAGE_SIZE) {
        __atomic_add_fetch(&g_loader.dll_base_next, dll_size - PAGE_SIZE, __ATOMIC_SEQ_CST);
    }

    /* Extract NT headers from image memory — reconstruct union from raw bytes */
    IMAGE_DOS_HEADER *img_dos = (IMAGE_DOS_HEADER *)base;
    uint32_t pe_off = img_dos->e_lfanew;

    /* Allocate NT headers on heap so they survive past this function.
     * Use INLINE_SYSCALL_MMAP directly — in the 32-bit build, glibc malloc/free
     * crash because they use FS-relative TLS access and FS→TEB is set. */
    void *nt_alloc = INLINE_SYSCALL_MMAP(NULL, PAGE_SIZE,
        PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (nt_alloc == MAP_FAILED) {
        INLINE_SYSCALL_MUNMAP(base, pe_size_of_image(&nt_copy));
        return NULL;
    }
    IMAGE_NT_HEADERS *img_nt = (IMAGE_NT_HEADERS *)nt_alloc;
    {
        uint32_t opt_off = pe_off + sizeof(uint32_t) + sizeof(IMAGE_FILE_HEADER);
        const uint16_t *magic = (const uint16_t *)((char *)base + opt_off);
        if (*magic == IMAGE_NT_OPTIONAL_HDR32_MAGIC) {
            img_nt->pe_type = PE_TYPE_32;
            syscall_safe_memcpy(&img_nt->u.nt32, (char *)base + pe_off, sizeof(IMAGE_NT_HEADERS32));
        } else {
            img_nt->pe_type = PE_TYPE_64;
            syscall_safe_memcpy(&img_nt->u.nt64, (char *)base + pe_off, sizeof(IMAGE_NT_HEADERS64));
        }
    }

    /* Check PE32/PE32+ mixing — DLL must match the main binary's PE type.
     * Use saved_is_32bit since map_image_at() overwrote g_loader.is_32bit with the DLL's type. */
    if (saved_is_32bit && img_nt->pe_type == PE_TYPE_64) {
        INLINE_SYSCALL_MUNMAP(nt_alloc, PAGE_SIZE);
        INLINE_SYSCALL_MUNMAP(base, pe_size_of_image(&nt_copy));
        return NULL;
    }
    if (!saved_is_32bit && img_nt->pe_type == PE_TYPE_32) {
        INLINE_SYSCALL_MUNMAP(nt_alloc, PAGE_SIZE);
        INLINE_SYSCALL_MUNMAP(base, pe_size_of_image(&nt_copy));
        return NULL;
    }

    /* Extract DLL name from path (hand-rolled strrchr) */
    const char *name = path;
    const char *p = path;
    while (*p) {
        if (*p == '/') name = p + 1;
        p++;
    }

    /* Register in module list */
    loaded_module_t *mod = add_module(base, name, img_nt);
    syscall_safe_debug_write_ptr(2, "load_dll: add_module=", mod ? (uintptr_t)mod : 0);
    if (mod == NULL) {
        uintptr_t sz = pe_size_of_image(img_nt);
        INLINE_SYSCALL_MUNMAP(nt_alloc, PAGE_SIZE);
        INLINE_SYSCALL_MUNMAP(base, sz);
        return NULL;
    }

    /* Add to PEB LDR */
    if (g_loader.peb_ldr != NULL) {
        ldr_add_module(mod);
        syscall_safe_debug_write_str(2, "load_dll: ldr_add=", "ok");
    }

    /* Resolve this DLL's own imports (recursive).
     * resolve_module_imports also calls parse_export_table internally. */
    int resolve_rc = resolve_module_imports(mod, depth + 1);
    syscall_safe_debug_write_str(2, "load_dll: resolve_imports=", resolve_rc == 0 ? "ok" : "fail");
    if (resolve_rc != 0) {
        /* Cleanup all resources allocated above */
        if (g_loader.peb_ldr != NULL && mod->ldr_linked) {
            ldr_remove_module(mod);
        }
        reset_export_cache(mod);
        remove_module(mod);
        uintptr_t sz = pe_size_of_image(img_nt);
        INLINE_SYSCALL_MUNMAP(nt_alloc, PAGE_SIZE);
        INLINE_SYSCALL_MUNMAP(base, sz);
        return NULL;
    }

    /* Parse exports if not already done (e.g., DLL has no imports but has exports) */
    IMAGE_DATA_DIRECTORY exp_dir;
    if (mod->export_cache.number_of_names == 0 &&
        pe_get_data_dir(img_nt, DIRECTORY_ENTRY_EXPORT, &exp_dir) &&  /* check export dir */
        exp_dir.VirtualAddress != 0) {
        parse_export_table(mod);
        syscall_safe_debug_write_str(2, "load_dll: parse_export=", "ok");
    }

    if (!call_module_dllmain(mod, DLL_PROCESS_ATTACH)) {
        if (g_loader.peb_ldr != NULL && mod->ldr_linked) {
            ldr_remove_module(mod);
        }
        reset_export_cache(mod);
        remove_module(mod);
        uintptr_t sz = pe_size_of_image(img_nt);
        INLINE_SYSCALL_MUNMAP(nt_alloc, PAGE_SIZE);
        INLINE_SYSCALL_MUNMAP(base, sz);
        return NULL;
    }
    mod->dllmain_called = 1;

    return mod;
}
