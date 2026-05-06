/*
 * kernel32_module.c — Module loading and management stubs
 *
 * All functions are WINE_STUB (ms_abi) so they can be called directly
 * by guest code. All loader functions are now glibc-free.
 */

#include <stdint.h>
#include <string.h>
#include <pthread.h>
#include <sys/mman.h>

#include "kernel32_priv.h"
#include "../loader/loader_priv.h"

/* ── LoadLibraryA ────────────────────────────────────────────── */

WINE_STUB
void *LoadLibraryA(const char *lpLibFileName)
{
    if (lpLibFileName == NULL)
        return NULL;

    loaded_module_t *mod = find_module_by_name_safe(lpLibFileName);
    if (mod != NULL) {
        mod->load_count++;
        return mod->base;
    }

    char path[512];
    if (!find_dll_path(lpLibFileName, path, sizeof(path)))
        return NULL;

    mod = load_dll(path, 0);
    if (mod == NULL)
        return NULL;

    return mod->base;
}

/* ── GetProcAddress ──────────────────────────────────────────── */

WINE_STUB
void *GetProcAddress(void *hModule, const char *lpProcName)
{
    if (hModule == NULL || lpProcName == NULL)
        return NULL;

    loaded_module_t *mod = find_module_by_addr(hModule);
    if (mod == NULL || mod->export_cache.number_of_names == 0)
        return NULL;

    return lookup_export(mod, lpProcName);
}

/* ── GetModuleHandleA ────────────────────────────────────────── */

WINE_STUB
void *GetModuleHandleA(const char *lpModuleName)
{
    if (lpModuleName == NULL) {
        if (module_count > 0)
            return module_list[0].base;
        return NULL;
    }

    loaded_module_t *mod = find_module_by_name_safe(lpModuleName);
    return mod ? mod->base : NULL;
}

/* ── FreeLibraryA ────────────────────────────────────────────── */

WINE_STUB
int FreeLibraryA(void *hModule)
{
    loaded_module_t *mod = find_module_by_addr(hModule);
    if (mod == NULL)
        return 0;

    mod->load_count--;
    if (mod->load_count > 0)
        return 1; /* Still referenced */

    /* Reset export cache (embedded, no free needed) */
    reset_export_cache(mod);

    /* Remove from LDR */
    if (g_peb_ldr != NULL && mod->ldr_linked)
        ldr_remove_module(mod);

    /* Unmap the image */
    if (mod->base && mod->nt) {
        uint32_t size = mod->nt->OptionalHeader.SizeOfImage;
        munmap(mod->base, size);
    }

    /* Remove from module list */
    remove_module(mod);

    return 1;
}

/* ── FreeLibraryAndExitThread ────────────────────────────────── */

WINE_STUB
__attribute__((noreturn)) void FreeLibraryAndExitThread(void *hModule, uint32_t exitCode)
{
    FreeLibraryA(hModule);
    pthread_exit((void *)(uintptr_t)exitCode);
}

/* ── SysV aliases for test code (ms_abi functions can't be called
 * from SysV code without argument passing mismatch) ──────────── */
void *_LoadLibraryA(const char *s)   { return LoadLibraryA(s); }
void *_GetProcAddress(void *m, const char *n) { return GetProcAddress(m, n); }
void *_GetModuleHandleA(const char *n) { return GetModuleHandleA(n); }
int _FreeLibraryA(void *m)           { return FreeLibraryA(m); }
