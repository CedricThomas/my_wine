/*
 * kernel32_module.c — Module loading and management stubs
 */

#include <stdint.h>
#include <string.h>
#include <sys/mman.h>

#include "kernel32_priv.h"
#include "include/common.h"
#include "src/pe_priv.h"
#include "../loader/loader_priv.h"
#include "../loader/loader_state.h"
#include "include/syscall_safe_utils.h"

static char g_dll_path[512];

/* ── LoadLibraryA ─────────────────────────────────────────────── */

KERNEL32_STUB
void *LoadLibraryA(const char *lpLibFileName)
{
    if (g_debug_level >= 2) {
        const char msg1[] = "LoadLibraryA: ENTER\n";
        INLINE_SYSCALL_WRITE(2, msg1, sizeof(msg1) - 1);
    }

    syscall_safe_debug_write_ptr(3, "LoadLibraryA: arg=", (uintptr_t)lpLibFileName);

    if (lpLibFileName == NULL) {
        syscall_safe_debug_write_ptr(2, "LoadLibraryA: ret=", 0);
        return FORCE_PTR_RETURN(NULL);
    }

    loaded_module_t *mod = find_module_by_name_safe(lpLibFileName);
    syscall_safe_debug_write_ptr(2, "LoadLibraryA: find_module=", mod ? (uintptr_t)mod->base : 0);
    if (mod != NULL) {
        mod->load_count++;
        syscall_safe_debug_write_ptr(2, "LoadLibraryA: ret=", (uintptr_t)mod->base);
        return FORCE_PTR_RETURN(mod->base);
    }

    int found = find_dll_path(lpLibFileName, g_dll_path, sizeof(g_dll_path));
    syscall_safe_debug_write_bool(2, "LoadLibraryA: find_path=", found);
    if (!found) {
        syscall_safe_debug_write_ptr(2, "LoadLibraryA: ret=", 0);
        return FORCE_PTR_RETURN(NULL);
    }

    mod = load_dll(g_dll_path, 0);
    if (mod == NULL) {
        syscall_safe_debug_write_ptr(2, "LoadLibraryA: ret=", 0);
        return FORCE_PTR_RETURN(NULL);
    }

    syscall_safe_debug_write_ptr(2, "LoadLibraryA: ret=", (uintptr_t)mod->base);
    return FORCE_PTR_RETURN(mod->base);
}

void *_LoadLibraryA(const char *lpLibFileName)
{
    return LoadLibraryA(lpLibFileName);
}

/* ── GetProcAddress ────────────────────────────────────────────── */

KERNEL32_STUB
void *GetProcAddress(void *hModule, const char *lpProcName)
{
    if (hModule == NULL || lpProcName == NULL)
        return FORCE_PTR_RETURN(NULL);

    loaded_module_t *mod = find_module_by_addr(hModule);
    if (mod == NULL || mod->export_cache.number_of_names == 0)
        return FORCE_PTR_RETURN(NULL);

    return FORCE_PTR_RETURN(lookup_export(mod, lpProcName));
}

void *_GetProcAddress(void *hModule, const char *lpProcName)
{
    return GetProcAddress(hModule, lpProcName);
}

/* ── GetModuleHandleA ──────────────────────────────────────────── */

KERNEL32_STUB
void *GetModuleHandleA(const char *lpModuleName)
{
    if (lpModuleName == NULL) {
        if (g_loader.module_count > 0)
            return FORCE_PTR_RETURN(g_loader.modules[0].base);
        return FORCE_PTR_RETURN(NULL);
    }

    loaded_module_t *mod = find_module_by_name_safe(lpModuleName);
    return FORCE_PTR_RETURN(mod ? mod->base : NULL);
}

void *_GetModuleHandleA(const char *lpModuleName)
{
    return GetModuleHandleA(lpModuleName);
}

KERNEL32_STUB
void *GetModuleHandleW(const uint16_t *lpModuleName)
{
    char narrow[260];
    size_t i = 0;

    if (lpModuleName == NULL)
        return GetModuleHandleA(NULL);

    while (lpModuleName[i] != 0 && i + 1 < sizeof(narrow)) {
        uint16_t ch = lpModuleName[i];
        if (ch > 0x7f)
            return FORCE_PTR_RETURN(NULL);
        narrow[i] = (char)ch;
        i++;
    }
    narrow[i] = '\0';
    return GetModuleHandleA(narrow);
}

/* ── FreeLibraryA ──────────────────────────────────────────────── */

KERNEL32_STUB
int FreeLibraryA(void *hModule)
{
    loaded_module_t *mod = find_module_by_addr(hModule);
    if (mod == NULL)
        return 0;

    mod->load_count--;
    if (mod->load_count > 0)
        return 1;

    reset_export_cache(mod);

    if (loader_get_peb_ldr() != NULL && mod->ldr_linked)
        ldr_remove_module(mod);

    if (mod->base && mod->nt) {
        uint32_t size = pe_size_of_image(mod->nt);
        munmap(mod->base, size);
    }

    remove_module(mod);

    return 1;
}

int _FreeLibraryA(void *hModule)
{
    return FreeLibraryA(hModule);
}

/* ── FreeLibraryAndExitThread ──────────────────────────────────── */

KERNEL32_STUB
__attribute__((noreturn)) void FreeLibraryAndExitThread(void *hModule, uint32_t exitCode)
{
    FreeLibraryA(hModule);
#ifdef MY_WINE32
    INLINE_SYSCALL_EXIT((int)exitCode);
#else
    _exit((int)exitCode);
#endif
}

void _FreeLibraryAndExitThread(void *hModule, uint32_t exitCode)
{
    FreeLibraryAndExitThread(hModule, exitCode);
}
