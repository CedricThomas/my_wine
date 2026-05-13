/*
 * import_table.c — Static import entry definitions
 *
 * Maintains the import_entry_t table mapping DLL/function names
 * to our stub implementations.
 */

#define _GNU_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <search.h>
#include <stdbool.h>

#include "include/ntdll.h"
#include "include/kernel32.h"
#include "include/msvcrt.h"
#include "include/common.h"

#include "loader_priv.h"
#include "include/pe_priv.h"
#include "include/debug.h"

/* Stub function declarations from crt_32_stub.c (available in both 32-bit and 64-bit builds) */
extern int ___lc_codepage_func(void);
extern int ___mb_cur_max_func(void);
extern int *_errno(void);
extern void _lock(int);
extern void _unlock(int);
extern void __getmainargs(int *, char ***, char ***, int, void *);
extern void _initterm(void);
extern void *_initterm_e(const void **, const void **);
extern void *_onexit(void (*)(void));
extern void __setusermatherr(void (*)(void));
extern void __set_app_type(int);
extern void __lconv_init(void);
extern void *__iob_func(void);
extern void *__acrt_iob_func(void);
extern int _m_fprintf(void *, const char *, ...);
extern int _m_fwrite(const void *, size_t, size_t, void *);
extern int _m_vfprintf(void *, const char *, void *);
extern int _m_fputc(int, void *);
extern struct lconv *_m_localeconv(void);
extern char *_m_strerror(int);
extern void _m_abort(void);
extern void _m_exit(int);
extern void *_m_malloc(size_t);
extern void _m_free(void *);
extern void *_m_calloc(size_t, size_t);
extern void *_m_realloc(void *, size_t);
extern void *_m_memcpy(void *, const void *, size_t);
extern void *_m_memset(void *, int, size_t);
extern size_t _m_strlen(const char *);
extern int _m_strcmp(const char *, const char *);
extern int _m_strncmp(const char *, const char *, size_t);
extern int _m_memcmp(const void *, const void *, size_t);
extern size_t _m_wcslen(const void *);
extern void *_m_signal(int, void (*)(int));
/* Wrapper function declarations from crt_32_stub.c (32-bit) and crt_globals.c (64-bit) */
extern char *__p__acmdln_func(void);
extern char *__p__fmode_func(void);
extern char *__p__commode_func(void);
extern char **__initenv_func(void);
/* Data symbols needed by import table — already declared in msvcrt.h (e.g. _acmdln, __p__acmdln) */
extern char **__initenv;
#ifdef MY_WINE_32
/* 32-bit only: these are data symbols (from crt_32_stub.c) */
extern char *__p__commode;
extern char *__p__fmode;
#endif

/* Name→address table for NT, kernel32 and msvcrt functions */
import_entry_t import_table[] = {
    /* ── ntdll syscall handlers ────────────────────────────────────────
     * Resolved via thunk lookup — each entry maps to a handler_*
     * function that dispatches to the corresponding Linux syscall.
     * All addresses are non-NULL (statically known at build time).
     * ───────────────────────────────────────────────────────────────── */
    { "ntdll.dll", "NtWriteFile", (void*)handler_NtWriteFile },
    { "ntdll.dll", "NtReadFile", (void*)handler_NtReadFile },
    { "ntdll.dll", "NtClose", (void*)handler_NtClose },
    { "ntdll.dll", "NtTerminateProcess", (void*)handler_NtTerminateProcess },
    { "ntdll.dll", "NtCallbackReturn", (void*)handler_NtCallbackReturn },
    { "ntdll.dll", "NtQueryInformationProcess", (void*)handler_NtQueryInformationProcess },
    { "ntdll.dll", "NtAllocateVirtualMemory", (void*)handler_NtAllocateVirtualMemory },
    { "ntdll.dll", "NtFreeVirtualMemory", (void*)handler_NtFreeVirtualMemory },
    { "ntdll.dll", "NtCreateSection", (void*)handler_NtCreateSection },
    { "ntdll.dll", "NtMapViewOfSection", (void*)handler_NtMapViewOfSection },
    { "ntdll.dll", "NtUnmapViewOfSection", (void*)handler_NtUnmapViewOfSection },
    { "ntdll.dll", "NtCreateEvent", (void*)handler_NtCreateEvent },
    { "ntdll.dll", "NtCreateSemaphore", (void*)handler_NtCreateSemaphore },
    { "ntdll.dll", "NtCreateThreadEx", (void*)handler_NtCreateThreadEx },
    { "ntdll.dll", "NtOpenFile", (void*)handler_NtOpenFile },
    { "ntdll.dll", "NtGetContextThread", (void*)handler_NtGetContextThread },
    { "ntdll.dll", "NtSetContextThread", (void*)handler_NtSetContextThread },
    /* ── kernel32 stubs ────────────────────────────────────────────────
     * Our C implementations of common Windows API functions.
     * All addresses are non-NULL (statically known at build time).
     * ───────────────────────────────────────────────────────────────── */
    { "kernel32.dll", "GetStdHandle", (void*)GetStdHandle },
    { "kernel32.dll", "CloseHandle", (void*)CloseHandle },
    { "kernel32.dll", "WriteFile", (void*)WriteFile },
    { "kernel32.dll", "ReadFile", (void*)ReadFile },
    { "kernel32.dll", "CreateFileA", (void*)CreateFileA },
    { "kernel32.dll", "DeleteFileA", (void*)DeleteFileA },
    { "kernel32.dll", "ExitProcess", (void*)ExitProcess },
    { "kernel32.dll", "GetProcAddress", (void*)GetProcAddress },
    { "kernel32.dll", "LoadLibraryA", (void*)LoadLibraryA },
    { "kernel32.dll", "GetModuleHandleA", (void*)GetModuleHandleA },
    { "kernel32.dll", "GetCommandLineA", (void*)GetCommandLineA },
    { "kernel32.dll", "GetEnvironmentStringsA", (void*)GetEnvironmentStringsA },
    /* mingw-w64 imports "FreeLibrary" (no 'A' suffix) — alias to FreeLibraryA */
    { "kernel32.dll", "FreeLibrary", (void*)FreeLibraryA },
    { "kernel32.dll", "FreeLibraryA", (void*)FreeLibraryA },
    { "kernel32.dll", "GetProcessHeap", (void*)GetProcessHeap },
    { "kernel32.dll", "lstrlenA", (void*)lstrlenA },
    { "kernel32.dll", "lstrcpyA", (void*)lstrcpyA },
    { "kernel32.dll", "lstrcatA", (void*)lstrcatA },
    { "kernel32.dll", "DeleteCriticalSection", (void*)DeleteCriticalSection },
    { "kernel32.dll", "EnterCriticalSection", (void*)EnterCriticalSection },
    { "kernel32.dll", "GetLastError", (void*)GetLastError },
    { "kernel32.dll", "GetStartupInfoA", (void*)GetStartupInfoA },
    { "kernel32.dll", "HeapAlloc", (void*)HeapAlloc },
    { "kernel32.dll", "HeapCreate", (void*)HeapCreate },
    { "kernel32.dll", "HeapDestroy", (void*)HeapDestroy },
    { "kernel32.dll", "HeapFree", (void*)HeapFree },
    { "kernel32.dll", "HeapReAlloc", (void*)HeapReAlloc },
    { "kernel32.dll", "HeapSize", (void*)HeapSize },
    { "kernel32.dll", "InitializeCriticalSection", (void*)InitializeCriticalSection },
    { "kernel32.dll", "LeaveCriticalSection", (void*)LeaveCriticalSection },
    { "kernel32.dll", "SetUnhandledExceptionFilter", (void*)SetUnhandledExceptionFilter },
    { "kernel32.dll", "Sleep", (void*)Sleep },
    { "kernel32.dll", "GetSystemTimeAsFileTime", (void*)GetSystemTimeAsFileTime },
    { "kernel32.dll", "QueryPerformanceCounter", (void*)QueryPerformanceCounter },
    { "kernel32.dll", "QueryPerformanceFrequency", (void*)QueryPerformanceFrequency },
    { "kernel32.dll", "CreateEventA", (void*)CreateEventA },
    { "kernel32.dll", "SetEvent", (void*)SetEvent },
    { "kernel32.dll", "ResetEvent", (void*)ResetEvent },
    { "kernel32.dll", "WaitForSingleObject", (void*)WaitForSingleObject },
    { "kernel32.dll", "CreateMutexA", (void*)CreateMutexA },
    { "kernel32.dll", "ReleaseMutex", (void*)ReleaseMutex },
    { "kernel32.dll", "TlsGetValue", (void*)TlsGetValue },
    { "kernel32.dll", "VirtualProtect", (void*)VirtualProtect },
    { "kernel32.dll", "VirtualQuery", (void*)VirtualQuery },
    { "kernel32.dll", "VirtualAlloc", (void*)VirtualAlloc },
    { "kernel32.dll", "VirtualFree", (void*)VirtualFree },
    { "kernel32.dll", "IsDBCSLeadByteEx", (void*)IsDBCSLeadByteEx },
    { "kernel32.dll", "MultiByteToWideChar", (void*)MultiByteToWideChar },
    { "kernel32.dll", "WideCharToMultiByte", (void*)WideCharToMultiByte },
    /* ── msvcrt functions (statically known) ────────────────────────────
     * Our C stubs for CRT initialization, I/O, and data variables.
     * All addresses are non-NULL (statically known at build time).
     * ───────────────────────────────────────────────────────────────── */
    { "msvcrt.dll", "__C_specific_handler", (void*)__C_specific_handler },
    /* CRT startup + stdlib — available in both 64-bit and 32-bit builds */
    { "msvcrt.dll", "__getmainargs", (void*)__getmainargs },
    { "msvcrt.dll", "__iob_func", (void*)__iob_func },
    { "msvcrt.dll", "__acrt_iob_func", (void*)__acrt_iob_func },
    { "msvcrt.dll", "__lconv_init", (void*)__lconv_init },
    { "msvcrt.dll", "__set_app_type", (void*)__set_app_type },
    { "msvcrt.dll", "__setusermatherr", (void*)__setusermatherr },
    { "msvcrt.dll", "_acmdln", (void*)&_acmdln },
    { "msvcrt.dll", "_amsg_exit", (void*)_amsg_exit },
    { "msvcrt.dll", "_cexit", (void*)_cexit },
    { "msvcrt.dll", "_commode", (void*)&_commode },
    { "msvcrt.dll", "_fmode", (void*)&_fmode },
    { "msvcrt.dll", "_initterm", (void*)_initterm },
    { "msvcrt.dll", "_onexit", (void*)_onexit },
    { "msvcrt.dll", "___lc_codepage_func", (void*)___lc_codepage_func },
    { "msvcrt.dll", "___mb_cur_max_func", (void*)___mb_cur_max_func },
    { "msvcrt.dll", "_errno", (void*)_errno },
    { "msvcrt.dll", "_lock", (void*)_lock },
    { "msvcrt.dll", "_unlock", (void*)_unlock },
    { "msvcrt.dll", "abort", (void*)_m_abort },
    { "msvcrt.dll", "exit", (void*)_m_exit },
    { "msvcrt.dll", "malloc", (void*)_m_malloc },
    { "msvcrt.dll", "free", (void*)_m_free },
    { "msvcrt.dll", "calloc", (void*)_m_calloc },
    { "msvcrt.dll", "realloc", (void*)_m_realloc },
    { "msvcrt.dll", "memcpy", (void*)_m_memcpy },
    { "msvcrt.dll", "memcmp", (void*)_m_memcmp },
    { "msvcrt.dll", "memset", (void*)_m_memset },
    { "msvcrt.dll", "strlen", (void*)_m_strlen },
    { "msvcrt.dll", "strcmp", (void*)_m_strcmp },
    { "msvcrt.dll", "strncmp", (void*)_m_strncmp },
    { "msvcrt.dll", "wcslen", (void*)_m_wcslen },
    { "msvcrt.dll", "signal", (void*)_m_signal },
    { "msvcrt.dll", "fprintf", (void*)_m_fprintf },
    { "msvcrt.dll", "fwrite", (void*)_m_fwrite },
    { "msvcrt.dll", "vfprintf", (void*)_m_vfprintf },
    { "msvcrt.dll", "fputc", (void*)_m_fputc },
    { "msvcrt.dll", "localeconv", (void*)_m_localeconv },
    { "msvcrt.dll", "strerror", (void*)_m_strerror },
#ifdef MY_WINE_32
    /* 32-bit: __p__* must be wrapper functions (JMP thunks in PE).
     * __initenv is DATA (PE writes to it, not calls it). */
    { "msvcrt.dll", "__initenv", (void*)&__initenv },
    { "msvcrt.dll", "__p__acmdln", (void*)__p__acmdln_func },
    { "msvcrt.dll", "__p__commode", (void*)__p__commode_func },
    { "msvcrt.dll", "__p__fmode", (void*)__p__fmode_func },
    { "msvcrt.dll", "_iob", (void*)__iob_func },
#else
    /* 64-bit: __p__* must also be functions. __initenv stays as data. */
    { "msvcrt.dll", "__initenv", (void*)&__initenv },
    { "msvcrt.dll", "__p__acmdln", (void*)__p__acmdln_func },
    { "msvcrt.dll", "__p__commode", (void*)__p__commode_func },
    { "msvcrt.dll", "__p__fmode", (void*)__p__fmode_func },
    { "msvcrt.dll", "_iob", (void*)__iob_func },
#endif
    { NULL, NULL, NULL }
};

size_t import_table_count = sizeof(import_table) / sizeof(import_entry_t) - 1;

void set_import(const char *name, void *address)
{
    for (int i = 0; import_table[i].name != NULL; i++) {
        if (strcmp(import_table[i].name, name) == 0) {
            import_table[i].address = address;
            return;
        }
    }
    DEBUG("ERROR: set_import: symbol '%s' not found", name);
}

static int import_entry_cmp(const void *a, const void *b)
{
    return strcmp(((const import_entry_t *)a)->name,
                  ((const import_entry_t *)b)->name);
}

/* Standalone 32-bit: can't use strcmp (libc TLS not initialized) */
#if defined(MY_WINE_32)
static int import_entry_cmp_nolibc(const char *a, const char *b)
{
    unsigned char ua, ub;
    while (*a && *b) {
        ua = (unsigned char)*a;
        ub = (unsigned char)*b;
        if (ua != ub) return ua - ub;
        a++; b++;
    }
    return (unsigned char)*a - (unsigned char)*b;
}
#endif

int import_cmp_by_name(const void *key, const void *elem)
{
#if defined(MY_WINE_32)
    return import_entry_cmp_nolibc((const char *)key, ((const import_entry_t *)elem)->name);
#else
    return strcmp((const char *)key, ((const import_entry_t *)elem)->name);
#endif
}

/**
 * Sort import_table by name for bsearch.
 */
void init_import_table(void)
{
    /* Sort import_table by name for bsearch. Exclude the sentinel entry. */
    size_t count = sizeof(import_table) / sizeof(import_entry_t) - 1;
#if defined(MY_WINE_32)
    /* Standalone 32-bit: can't use qsort/strcmp (libc TLS not initialized).
     * Use insertion sort with local strcmp that doesn't need libc. */
    for (size_t i = 1; i < count; i++) {
        import_entry_t key = import_table[i];
        size_t j = i;
        while (j > 0 && import_entry_cmp_nolibc(import_table[j-1].name, key.name) > 0) {
            import_table[j] = import_table[j-1];
            j--;
        }
        import_table[j] = key;
    }
#else
    qsort(import_table, count, sizeof(import_entry_t), import_entry_cmp);
#endif
}

/**
 * Build a flat array of (ILT value, resolved address, func name) from
 * all import descriptors, in DLL order.
 * Returns the number of flat entries created.
 */
int build_flat_import_array(void *base, IMAGE_NT_HEADERS *nt,
                            struct import_flat flat[])
{
    IMAGE_DATA_DIRECTORY imp_dir;
    if (!pe_get_import_dir(nt, &imp_dir)) return 0;
    uint64_t import_rva = imp_dir.VirtualAddress;
    IMAGE_IMPORT_DESCRIPTOR *desc_start = (IMAGE_IMPORT_DESCRIPTOR *)((char *)base + import_rva);

    bool is32 = pe_is_pe32(nt);
    int num_flat = 0;
    IMAGE_IMPORT_DESCRIPTOR *desc = desc_start;
    while (desc->Name != 0 && num_flat < MAX_FLAT_IMPORTS) {
        const char *dll_name = (const char *)((char *)base + desc->Name);

        if (is32) {
            IMAGE_THUNK_DATA32 *orig_thunks = (IMAGE_THUNK_DATA32 *)((char *)base + desc->u1.OriginalFirstThunk);
            IMAGE_THUNK_DATA32 *iath = (IMAGE_THUNK_DATA32 *)((char *)base + desc->FirstThunk);

            for (int i = 0; orig_thunks[i].AddressOfData != 0 && num_flat < MAX_FLAT_IMPORTS; i++) {
                flat[num_flat].ilt_value = orig_thunks[i].AddressOfData;
                flat[num_flat].resolved_addr = (uint64_t)(uint32_t)iath[i].AddressOfData;
                flat[num_flat].iat_addr = (uint64_t)(uintptr_t)&iath[i].AddressOfData;
                flat[num_flat].dll_name = dll_name;
                if (orig_thunks[i].AddressOfData & 0x80000000) {
                    uint16_t ordinal = (uint16_t)(orig_thunks[i].AddressOfData & 0xFFFF);
                    const char *fname = ordinal_lookup(dll_name, ordinal);
                    flat[num_flat].func_name = fname ? fname : "<ordinal>";
                } else {
                    IMAGE_IMPORT_BY_NAME *imp_name = (IMAGE_IMPORT_BY_NAME *)((char *)base + orig_thunks[i].AddressOfData);
                    flat[num_flat].func_name = (const char *)imp_name->Name;
                }
                num_flat++;
            }
        } else {
            IMAGE_THUNK_DATA64 *orig_thunks = (IMAGE_THUNK_DATA64 *)((char *)base + desc->u1.OriginalFirstThunk);
            IMAGE_THUNK_DATA64 *iath = (IMAGE_THUNK_DATA64 *)((char *)base + desc->FirstThunk);

            for (int i = 0; orig_thunks[i].AddressOfData != 0 && num_flat < MAX_FLAT_IMPORTS; i++) {
                flat[num_flat].ilt_value = orig_thunks[i].AddressOfData;
                flat[num_flat].resolved_addr = iath[i].AddressOfData;
                flat[num_flat].iat_addr = (uint64_t)(uintptr_t)&iath[i].AddressOfData;
                flat[num_flat].dll_name = dll_name;
                if (orig_thunks[i].AddressOfData & 0x8000000000000000ULL) {
                    uint16_t ordinal = (uint16_t)(orig_thunks[i].AddressOfData & 0xFFFF);
                    const char *fname = ordinal_lookup(dll_name, ordinal);
                    flat[num_flat].func_name = fname ? fname : "<ordinal>";
                } else {
                    IMAGE_IMPORT_BY_NAME *imp_name = (IMAGE_IMPORT_BY_NAME *)((char *)base + orig_thunks[i].AddressOfData);
                    flat[num_flat].func_name = (const char *)imp_name->Name;
                }
                num_flat++;
            }
        }
        desc++;
    }

    return num_flat;
}

/**
 * Strategy 1: target overlaps with a resolved import address.
 * Only check the flat entry whose iat_addr matches the target IAT entry.
 * Does NOT write -- the value is already correct (from pass 1 IAT).
 */
bool strategy_resolved_overlap(uint64_t current_val, void *target_ptr,
                               struct import_flat *flat, int num_flat)
{
    uint64_t target_addr = (uint64_t)(uintptr_t)target_ptr;
    for (int f = 0; f < num_flat; f++) {
        if (flat[f].iat_addr == target_addr && flat[f].resolved_addr == current_val) {
            return true;
        }
    }
    return false;
}

/**
 * Strategy 2: ILT entry value equals a resolved address.
 * Only match the flat entry whose iat_addr matches the target IAT entry.
 * If current_val matches an ilt_value whose resolved_addr is set,
 * write the resolved_addr to the target location.
 */
bool strategy_ilt_value_match(void *target_ptr, uint64_t current_val,
                              uint64_t target, size_t thunk_size,
                              struct import_flat *flat, int num_flat)
{
    if (current_val == 0)
        return false;
    uint64_t target_addr = (uint64_t)(uintptr_t)target_ptr;
    for (int f = 0; f < num_flat; f++) {
        if (flat[f].iat_addr == target_addr &&
            flat[f].ilt_value == current_val && flat[f].resolved_addr != 0) {
            memcpy(target_ptr, &flat[f].resolved_addr, thunk_size);
            DEBUG("    Thunk patch (ilt match): %s!%s at 0x%lx <- 0x%lx",
                   flat[f].dll_name, flat[f].func_name,
                   (unsigned long)target, (unsigned long)flat[f].resolved_addr);
            return true;
        }
    }
    return false;
}

/**
 * Strategy 3: Direct IAT address lookup via per-descriptor iat_addr.
 * Find the flat entry whose iat_addr matches the target IAT entry address.
 * This provides exact per-descriptor IAT range awareness -- each entry
 * knows which DLL's IAT it belongs to by its iat_addr.
 */
bool strategy_ilt_offset_match(void *target_ptr, uint64_t target,
                               uint64_t current_val, size_t thunk_size,
                               struct import_flat *flat, int num_flat)
{
    if (current_val == 0)
        return false;

    uint64_t target_addr = (uint64_t)(uintptr_t)target_ptr;
    for (int f = 0; f < num_flat; f++) {
        if (flat[f].iat_addr == target_addr && flat[f].resolved_addr != 0) {
            memcpy(target_ptr, &flat[f].resolved_addr, thunk_size);
            DEBUG("    Thunk patch (ilt-offset match): %s!%s at 0x%lx <- 0x%lx",
                   flat[f].dll_name, flat[f].func_name,
                   (unsigned long)target, (unsigned long)flat[f].resolved_addr);
            return true;
        }
    }

    return false;
}


