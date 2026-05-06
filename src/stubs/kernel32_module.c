/*
 * kernel32_module.c — Module loading and management stubs
 *
 * All functions are WINE_STUB (ms_abi) for guest code,
 * with plain-C aliases for test code.
 * All loader functions are now glibc-free.
 *
 * NOTE: There is a known GCC bug where WINE_STUB functions
 * (ms_abi + force_align_arg_pointer) that return high addresses
 * (> 4GB) get truncated to 32 bits on return to guest code.
 * Workaround: ensure DLLs are mapped below 4GB by using
 * MAP_FIXED with a controlled base address in load_dll().
 */

#include <stdint.h>
#include <string.h>
#include <pthread.h>
#include <sys/mman.h>

#include "kernel32_priv.h"
#include "../loader/loader_priv.h"

/* Global DLL search buffer (avoids stack allocation issues) */
static char g_dll_path[512];

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

    if (!find_dll_path(lpLibFileName, g_dll_path, sizeof(g_dll_path)))
        return NULL;

    mod = load_dll(g_dll_path, 0);
    if (mod == NULL)
        return NULL;

    return mod->base;
}

void *_LoadLibraryA(const char *lpLibFileName) { return LoadLibraryA(lpLibFileName); }

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

void *_GetProcAddress(void *hModule, const char *lpProcName)
{
    return GetProcAddress(hModule, lpProcName);
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

void *_GetModuleHandleA(const char *lpModuleName)
{
    return GetModuleHandleA(lpModuleName);
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
        return 1;

    reset_export_cache(mod);

    if (g_peb_ldr != NULL && mod->ldr_linked)
        ldr_remove_module(mod);

    if (mod->base && mod->nt) {
        uint32_t size = mod->nt->OptionalHeader.SizeOfImage;
        munmap(mod->base, size);
    }

    remove_module(mod);

    return 1;
}

int _FreeLibraryA(void *hModule) { return FreeLibraryA(hModule); }

/* ── FreeLibraryAndExitThread ────────────────────────────────── */

WINE_STUB
__attribute__((noreturn)) void FreeLibraryAndExitThread(void *hModule, uint32_t exitCode)
{
    FreeLibraryA(hModule);
    pthread_exit((void *)(uintptr_t)exitCode);
}
