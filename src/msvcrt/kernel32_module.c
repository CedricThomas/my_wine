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
#include "../loader/import_table.h"
#include "include/syscall_safe_utils.h"

static char g_dll_path[512];

#define DLL_PROCESS_DETACH 0

typedef int (KERNEL32_ABI *dll_entry_fn_t)(void *, uint32_t, void *);

typedef struct {
    const char *name;
    uintptr_t handle;
} builtin_module_t;

static const builtin_module_t g_builtin_modules[] = {
    { "kernel32.dll", 0xffff0001u },
    { "ntdll.dll",    0xffff0002u },
    { "msvcrt.dll",   0xffff0003u },
    { "advapi32.dll", 0xffff0004u },
    { "user32.dll",   0xffff0005u },
    { "gdi32.dll",    0xffff0006u },
    { "winmm.dll",    0xffff0007u },
    { "ddraw.dll",    0xffff0008u },
    { "dsound.dll",   0xffff0009u },
    { "dplay.dll",    0xffff000au },
    { "comdlg32.dll", 0xffff000bu },
    { "comctl32.dll", 0xffff000cu },
    { NULL, 0 }
};

static const builtin_module_t *find_builtin_module_by_name(const char *name)
{
    int i;

    if (!name)
        return NULL;

    for (i = 0; g_builtin_modules[i].name != NULL; i++) {
        if (syscall_safe_strcasecmp(g_builtin_modules[i].name, name) == 0)
            return &g_builtin_modules[i];
    }

    return NULL;
}

static void invoke_module_dllmain_detach(dll_entry_fn_t entry, void *base)
{
#if defined(__i386__)
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
        :
        : [entry] "r"(entry), [base] "r"(base), [reason] "i"(DLL_PROCESS_DETACH)
        : "eax", "ecx", "edx", "memory", "cc");
#else
    entry(base, DLL_PROCESS_DETACH, NULL);
#endif
}

static void call_module_dllmain_detach(loaded_module_t *mod)
{
    uint32_t entry_rva;
    dll_entry_fn_t entry;

    if (mod == NULL || !mod->dllmain_called || mod->base == NULL || mod->nt == NULL)
        return;

    entry_rva = pe_entry_rva(mod->nt);
    if (entry_rva == 0)
        return;

    entry = (dll_entry_fn_t)((uint8_t *)mod->base + entry_rva);
    invoke_module_dllmain_detach(entry, mod->base);
    mod->dllmain_called = 0;
}

static const builtin_module_t *find_builtin_module_by_handle(void *handle)
{
    uintptr_t value = (uintptr_t)handle;
    int i;

    for (i = 0; g_builtin_modules[i].name != NULL; i++) {
        if (g_builtin_modules[i].handle == value)
            return &g_builtin_modules[i];
    }

    return NULL;
}

static void *lookup_builtin_export(const char *dll_name, const char *func_name)
{
    size_t i;

    if (!dll_name || !func_name)
        return NULL;

    for (i = 0; i < import_table_count; i++) {
        if (!import_table[i].dll_name || !import_table[i].name || !import_table[i].address)
            continue;
        if (syscall_safe_strcasecmp(import_table[i].dll_name, dll_name) != 0)
            continue;
        if (syscall_safe_strcmp(import_table[i].name, func_name) != 0)
            continue;
        return import_table[i].address;
    }

    return NULL;
}

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

    const builtin_module_t *builtin = find_builtin_module_by_name(lpLibFileName);
    if (builtin != NULL) {
        syscall_safe_debug_write_ptr(2, "LoadLibraryA: ret=", builtin->handle);
        return FORCE_PTR_RETURN((void *)builtin->handle);
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
    void *result;

    syscall_safe_debug_write_ptr(2, "GetProcAddress: module=", (uintptr_t)hModule);
    syscall_safe_debug_write_str(2, "GetProcAddress: name=", lpProcName ? lpProcName : "(null)");

    if (hModule == NULL || lpProcName == NULL)
        return FORCE_PTR_RETURN(NULL);

    const builtin_module_t *builtin = find_builtin_module_by_handle(hModule);
    if (builtin != NULL) {
        result = lookup_builtin_export(builtin->name, lpProcName);
        syscall_safe_debug_write_ptr(2, "GetProcAddress: ret=", (uintptr_t)result);
        return FORCE_PTR_RETURN(result);
    }

    loaded_module_t *mod = find_module_by_addr(hModule);
    if (mod == NULL || mod->export_cache.number_of_names == 0)
        return FORCE_PTR_RETURN(NULL);

    result = lookup_export(mod, lpProcName);
    syscall_safe_debug_write_ptr(2, "GetProcAddress: ret=", (uintptr_t)result);
    return FORCE_PTR_RETURN(result);
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

    const builtin_module_t *builtin = find_builtin_module_by_name(lpModuleName);
    if (builtin != NULL)
        return FORCE_PTR_RETURN((void *)builtin->handle);

    loaded_module_t *mod = find_module_by_name_safe(lpModuleName);
    return FORCE_PTR_RETURN(mod ? mod->base : NULL);
}

void *_GetModuleHandleA(const char *lpModuleName)
{
    return GetModuleHandleA(lpModuleName);
}

KERNEL32_STUB
uint32_t GetModuleFileNameA(void *hModule, char *lpFilename, uint32_t nSize)
{
    const char *src = NULL;
    loaded_module_t *mod;
    uint32_t len = 0;

    if (!lpFilename || nSize == 0)
        return 0;

    if (!hModule || hModule == g_loader.image_base)
        src = g_loader.pe_path;
    else {
        mod = find_module_by_addr(hModule);
        if (mod)
            src = mod->name;
    }

    if (!src)
        src = g_loader.pe_path;

    while (src[len] && len + 1 < nSize) {
        lpFilename[len] = src[len];
        len++;
    }
    lpFilename[len] = '\0';
    return len;
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
    if (find_builtin_module_by_handle(hModule) != NULL)
        return 1;

    loaded_module_t *mod = find_module_by_addr(hModule);
    if (mod == NULL)
        return 0;

    mod->load_count--;
    if (mod->load_count > 0)
        return 1;

    call_module_dllmain_detach(mod);
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
