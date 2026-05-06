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

/* ── UNIX stack helpers ────────────────────────────────────────
 *
 * After GS→TEB switch, glibc functions (strcasecmp, getenv, snprintf,
 * access, fprintf) access vDSO via GS-relative offsets and read TEB
 * memory instead → SIGSEGV. We switch to the UNIX stack (same one
 * used by the syscall dispatcher) before calling any glibc-dependent
 * function, then switch back.
 */

extern void *unix_stack_ptr_val;  /* from dispatcher_entry.c */

/* Type for host-side functions: void *(void *arg) */
typedef void *(*wine_host_fn)(void *);

/* call_on_unix_stack — switch to UNIX stack + host GS, call fn(arg), switch back.
 * The function must be a normal SysV C function (NOT ms_abi).
 * NOINLINE: prevents the compiler from merging this into an ms_abi frame
 * and using the wrong calling convention for the fn() call.
 *
 * After finalize_guest_state, GS points to the TEB. glibc functions access
 * TLS via GS-relative offsets. We switch GS to the host value, call the
 * function on the UNIX stack, then restore everything. */
static __attribute__((noinline)) void *call_on_unix_stack(wine_host_fn fn, void *arg)
{
    uintptr_t guest_rsp;
    uintptr_t guest_gs_base;

    /* Save guest RSP and GS base (currently TEB) */
    __asm__ volatile(
        "mov %%rsp, %0\n"
        "rdgsbase %1\n"
        : "=r"(guest_rsp), "=r"(guest_gs_base)
    );

    /* Switch to UNIX stack */
    __asm__ volatile("mov %0, %%rsp; sub $8, %%rsp" : : "r"(unix_stack_ptr_val) : "memory");

    /* Restore host GS base so glibc can access TLS/vDSO */
    __asm__ volatile("wrgsbase %0" : : "r"(g_host_gs_base));

    /* Call host function (glibc is now safe) */
    void *ret = fn(arg);

    /* Restore guest RSP */
    __asm__ volatile("mov %0, %%rsp" : : "r"(guest_rsp) : "memory");

    /* Restore guest GS base (TEB) */
    __asm__ volatile("wrgsbase %0" : : "r"(guest_gs_base));

    return ret;
}

/* Host-side LoadLibraryA implementation (SysV, glibc OK) */
static void *load_library_host(void *arg)
{
    const char *name = (const char *)arg;
    char path[512];
    if (!find_dll_path(name, path, sizeof(path)))
        return NULL;
    loaded_module_t *mod = load_dll(path, 0);
    return mod ? (void *)mod : NULL;
}

/* ── LoadLibraryA ────────────────────────────────────────────── */

/* Safe version of find_module_by_name that doesn't use strcasecmp (glibc/vDSO). */
static loaded_module_t *find_module_by_name_safe(const char *name)
{
    for (int i = 0; i < module_count; i++) {
        const char *a = module_list[i].name;
        const char *b = name;
        if (a == NULL || b == NULL) continue;
        while (*a && *b) {
            unsigned char ca = *a, cb = *b;
            if (ca >= 'A' && ca <= 'Z') ca += 32;
            if (cb >= 'A' && cb <= 'Z') cb += 32;
            if (ca != cb) break;
            a++; b++;
        }
        if (*a == '\0' && *b == '\0') return &module_list[i];
    }
    return NULL;
}

WINE_STUB
void *LoadLibraryA(const char *lpLibFileName)
{
    if (lpLibFileName == NULL)
        return NULL;

    /* Fast path: already loaded (no glibc, no stack switch) */
    loaded_module_t *mod = find_module_by_name_safe(lpLibFileName);
    if (mod != NULL) {
        mod->load_count++;
        return mod->base;
    }

    /* Slow path: switch to UNIX stack for glibc-dependent load_dll */
    void *ret = call_on_unix_stack(load_library_host, (void *)lpLibFileName);
    if (ret == NULL)
        return NULL;

    mod = (loaded_module_t *)ret;
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
