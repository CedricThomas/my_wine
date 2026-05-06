/*
 * kernel32_module.c — Module loading and management stubs
 *
 * Implementations of LoadLibraryA, GetProcAddress, GetModuleHandleA,
 * FreeLibraryA, and FreeLibraryAndExitThread.
 */

#define _GNU_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <dlfcn.h>
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

    /* Check if already loaded */
    loaded_module_t *mod = find_module_by_name(lpLibFileName);
    if (mod != NULL) {
        /* Duplicate load — increment ref count and return cached base */
        mod->load_count++;
        return mod->base;
    }

    /* Search for the DLL */
    char path[512];
    if (!find_dll_path(lpLibFileName, path, sizeof(path))) {
        fprintf(stderr, "LoadLibraryA: cannot find '%s'\n", lpLibFileName);
        return NULL;
    }

    /* Load the DLL (map, relocate, register, resolve imports) */
    mod = load_dll(path, 0);
    if (mod == NULL) {
        fprintf(stderr, "LoadLibraryA: failed to load '%s'\n", lpLibFileName);
        return NULL;
    }

    return mod->base;
}

/* ── GetProcAddress ──────────────────────────────────────────── */

WINE_STUB
void *GetProcAddress(void *hModule, const char *lpProcName)
{
    if (hModule == NULL || lpProcName == NULL)
        return NULL;

    /* Find the module containing hModule */
    loaded_module_t *mod = find_module_by_addr(hModule);
    if (mod == NULL || mod->export_cache == NULL)
        return NULL;

    return lookup_export(mod, lpProcName);
}

/* ── GetModuleHandleA ────────────────────────────────────────── */

WINE_STUB
void *GetModuleHandleA(const char *lpModuleName)
{
    /* NULL means return the main module */
    if (lpModuleName == NULL) {
        if (module_count > 0)
            return module_list[0].base;
        return NULL;
    }

    loaded_module_t *mod = find_module_by_name(lpModuleName);
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

    /* Free export cache */
    if (mod->export_cache) {
        free_export_cache(mod->export_cache);
        mod->export_cache = NULL;
    }

    /* Remove from LDR */
    if (g_peb_ldr != NULL && mod->ldr_entry != NULL)
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
