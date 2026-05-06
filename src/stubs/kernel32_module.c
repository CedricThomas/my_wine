/*
 * kernel32_module.c — Module loading and management stubs
 *
 * Implementations of LoadLibraryA, GetProcAddress, GetModuleHandleA,
 * FreeLibraryA, and FreeLibraryAndExitThread.
 *
 * All loader functions (load_dll, find_dll_path, etc.) are now glibc-free,
 * so no stack switching is needed. LoadLibraryA calls load_dll directly
 * from the guest stack.
 *
 * Each stub has a SysV implementation + ms_abi wrapper for guest code.
 * The SysV implementation can be called directly from test code.
 */

#include <stdint.h>
#include <string.h>
#include <pthread.h>
#include <sys/mman.h>

#include "kernel32_priv.h"
#include "../loader/loader_priv.h"

/* ── SysV implementations (also callable from tests) ─────────── */

void *_LoadLibraryA(const char *lpLibFileName)
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

WINE_STUB
void *LoadLibraryA(const char *lpLibFileName)
{
    return _LoadLibraryA(lpLibFileName);
}

/* ── GetProcAddress ──────────────────────────────────────────── */

void *_GetProcAddress(void *hModule, const char *lpProcName)
{
    if (hModule == NULL || lpProcName == NULL)
        return NULL;

    loaded_module_t *mod = find_module_by_addr(hModule);
    if (mod == NULL || mod->export_cache.number_of_names == 0)
        return NULL;

    return lookup_export(mod, lpProcName);
}

WINE_STUB
void *GetProcAddress(void *hModule, const char *lpProcName)
{
    return _GetProcAddress(hModule, lpProcName);
}

/* ── GetModuleHandleA ────────────────────────────────────────── */

void *_GetModuleHandleA(const char *lpModuleName)
{
    if (lpModuleName == NULL) {
        if (module_count > 0)
            return module_list[0].base;
        return NULL;
    }

    loaded_module_t *mod = find_module_by_name_safe(lpModuleName);
    return mod ? mod->base : NULL;
}

WINE_STUB
void *GetModuleHandleA(const char *lpModuleName)
{
    return _GetModuleHandleA(lpModuleName);
}

/* ── FreeLibraryA ────────────────────────────────────────────── */

int _FreeLibraryA(void *hModule)
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

WINE_STUB
int FreeLibraryA(void *hModule)
{
    return _FreeLibraryA(hModule);
}

/* ── FreeLibraryAndExitThread ────────────────────────────────── */

WINE_STUB
__attribute__((noreturn)) void FreeLibraryAndExitThread(void *hModule, uint32_t exitCode)
{
    _FreeLibraryA(hModule);
    pthread_exit((void *)(uintptr_t)exitCode);
}

/* ── Public aliases for SysV callers (tests, internal code) ──── */
/* These must be declared in kernel32.h so test code can call them */
