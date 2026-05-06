/*
 * import_table.c — Import resolution table
 *
 * This file maintains the name→address lookup table used to resolve
 * PE import descriptors against our stub implementations.  Each entry
 * maps a DLL name + function name to a code address.
 *
 * ── Resolution Tiers ──────────────────────────────────────────────────
 *
 *   Tier 1 (THUNK)   — ntdll.dll syscall handlers
 *       Resolved via thunk lookup to our syscall dispatcher.
 *       Functions here never go through the real ntdll.dll.
 *
 *   Tier 2 (STUB)    — kernel32.dll C implementations
 *       Hand-written stubs that provide enough API surface for the
 *       target executable to operate.  Includes mingw-w64 aliases.
 *
 *   Tier 3 (Msvcrt)  — msvcrt.dll functions
 *       Split into two sub-tiers:
 *         Tier 3a — statically-known addresses: functions we implement
 *                  ourselves or expose data symbols (NULL-terminated).
 *         Tier 3b — dynamically-filled entries: initialised at runtime
 *                  by init_msvcrt_imports(), which fills each entry
 *                  with our own C reimplementation (e.g. malloc →
 *                  wine_malloc, strlen → wine_strlen).
 *
 *   init_msvcrt_imports() fills the NULL entries in Tier 3b by calling
 *   set_import() with our internal msvcrt-compatible implementations.
 *
 *   import_table_count is computed as sizeof(table)/sizeof(entry) - 1
 *   (excludes the sentinel).  init_import_table() sorts the array by
 *   name for bsearch lookups.
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
#include "include/debug.h"

/*
 * Resolution strategy for import entries.
 * THUNK    — ntdll syscall handlers (dispatched to our syscall layer)
 * STUB     — kernel32/mingw stubs (our C implementations)
 * DYNAMIC  — msvcrt functions filled at runtime by init_msvcrt_imports()
 *
 * NOTE: This enum is for documentation only.  It is NOT added to
 *       import_entry_t (that would change the ABI).  Future code can
 *       reference it if a richer resolution model is needed.
 */
typedef enum {
    RESOLVE_THUNK,      /* ntdll syscall thunks */
    RESOLVE_STUB,       /* kernel32 C stubs */
    RESOLVE_DYNAMIC,    /* msvcrt — filled by init_msvcrt_imports() */
} resolution_type;

/*
 * Import table — name → address lookup.
 * Sorted by name at init time (init_import_table) for bsearch.
 * Sentinel entry {NULL, NULL, NULL} marks the end.
 */
import_entry_t import_table[] = {
    /* ── Tier 1: ntdll syscall thunks (RESOLVE_THUNK) ── */
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
    { "ntdll.dll", "NtCreateThreadEx", (void*)handler_NtCreateThreadEx },
    { "ntdll.dll", "NtOpenFile", (void*)handler_NtOpenFile },
    { "ntdll.dll", "NtGetContextThread", (void*)handler_NtGetContextThread },
    { "ntdll.dll", "NtSetContextThread", (void*)handler_NtSetContextThread },
    /* ── Tier 2: kernel32 C stubs (RESOLVE_STUB) ── */
    { "kernel32.dll", "GetStdHandle", (void*)GetStdHandle },
    { "kernel32.dll", "WriteFile", (void*)WriteFile },
    { "kernel32.dll", "ReadFile", (void*)ReadFile },
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
    { "kernel32.dll", "IsDBCSLeadByteEx", (void*)IsDBCSLeadByteEx },
    { "kernel32.dll", "MultiByteToWideChar", (void*)MultiByteToWideChar },
    { "kernel32.dll", "WideCharToMultiByte", (void*)WideCharToMultiByte },
    { "msvcrt.dll", "__C_specific_handler", (void*)__C_specific_handler },
    /* ── Tier 3a: msvcrt statically-known (RESOLVE_STUB) ── */
    { "msvcrt.dll", "__getmainargs", (void*)__getmainargs },
    { "msvcrt.dll", "__initenv", (void*)__initenv },
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
    /* ── Tier 3b: msvcrt dynamic entries (RESOLVE_DYNAMIC) — filled by init_msvcrt_imports() ── */
    { "msvcrt.dll", "abort", NULL },
    { "msvcrt.dll", "calloc", NULL },
    { "msvcrt.dll", "exit", NULL },
    { "msvcrt.dll", "fprintf", NULL },
    { "msvcrt.dll", "free", NULL },
    { "msvcrt.dll", "fwrite", NULL },
    { "msvcrt.dll", "malloc", NULL },
    { "msvcrt.dll", "memcpy", NULL },
    { "msvcrt.dll", "realloc", NULL },
    { "msvcrt.dll", "signal", NULL },
    { "msvcrt.dll", "strlen", NULL },
    { "msvcrt.dll", "strncmp", NULL },
    { "msvcrt.dll", "vfprintf", NULL },
    { "msvcrt.dll", "___lc_codepage_func", NULL },
    { "msvcrt.dll", "___mb_cur_max_func", NULL },
    { "msvcrt.dll", "_errno", NULL },
    { "msvcrt.dll", "_lock", NULL },
    { "msvcrt.dll", "_unlock", NULL },
    { "msvcrt.dll", "fputc", NULL },
    { "msvcrt.dll", "localeconv", NULL },
    { "msvcrt.dll", "strerror", NULL },
    { "msvcrt.dll", "wcslen", NULL },
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

int import_cmp_by_name(const void *key, const void *elem)
{
    return strcmp((const char *)key, ((const import_entry_t *)elem)->name);
}

/**
 * Sort import_table by name for bsearch.
 */
void init_import_table(void)
{
    /* Sort import_table by name for bsearch. Exclude the sentinel entry. */
    size_t count = sizeof(import_table) / sizeof(import_entry_t) - 1;
    qsort(import_table, count, sizeof(import_entry_t), import_entry_cmp);
}

/**
 * Build a flat array of (ILT value, resolved address, func name) from
 * all import descriptors, in DLL order.
 * Returns the number of flat entries created.
 */
int build_flat_import_array(void *base, IMAGE_NT_HEADERS64 *nt,
                            struct import_flat flat[])
{
    IMAGE_OPTIONAL_HEADER64 *opt = &nt->OptionalHeader;
    uint64_t import_rva = opt->DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT].VirtualAddress;
    IMAGE_IMPORT_DESCRIPTOR *desc_start = (IMAGE_IMPORT_DESCRIPTOR *)((char *)base + import_rva);

    int num_flat = 0;
    IMAGE_IMPORT_DESCRIPTOR *desc = desc_start;
    while (desc->Name != 0 && num_flat < MAX_FLAT_IMPORTS) {
        const char *dll_name = (const char *)((char *)base + desc->Name);
        IMAGE_THUNK_DATA64 *orig_thunks = (IMAGE_THUNK_DATA64 *)((char *)base + desc->u1.OriginalFirstThunk);
        IMAGE_THUNK_DATA64 *iath = (IMAGE_THUNK_DATA64 *)((char *)base + desc->FirstThunk);

        for (int i = 0; orig_thunks[i].AddressOfData != 0 && num_flat < MAX_FLAT_IMPORTS; i++) {
            flat[num_flat].ilt_value = orig_thunks[i].AddressOfData;
            flat[num_flat].resolved_addr = iath[i].AddressOfData;
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
        desc++;
    }

    return num_flat;
}

/**
 * Strategy 1: target overlaps with a resolved import address.
 * Check if current value matches any resolved_addr in the flat array.
 * Does NOT write -- the value is already correct (from pass 1 IAT).
 */
bool strategy_resolved_overlap(uint64_t current_val,
                               struct import_flat *flat, int num_flat)
{
    for (int f = 0; f < num_flat; f++) {
        if (flat[f].resolved_addr == current_val) {
            return true;
        }
    }
    return false;
}

/**
 * Strategy 2: ILT entry value equals a resolved address.
 * If current_val matches an ilt_value whose resolved_addr is set,
 * write the resolved_addr to the target location.
 */
bool strategy_ilt_value_match(uint64_t *target_ptr, uint64_t current_val,
                              uint64_t target,
                              struct import_flat *flat, int num_flat)
{
    if (current_val == 0)
        return false;
    for (int f = 0; f < num_flat; f++) {
        if (flat[f].ilt_value == current_val && flat[f].resolved_addr != 0) {
            *target_ptr = flat[f].resolved_addr;
            DEBUG("    Thunk patch (ilt match): %s!%s at 0x%lx <- 0x%lx",
                   flat[f].dll_name, flat[f].func_name,
                   (unsigned long)target, (unsigned long)flat[f].resolved_addr);
            return true;
        }
    }
    return false;
}

/**
 * Strategy 3: ILT offset+slot matches thunk position.
 * If target falls within the import directory, compute slot index
 * and use the corresponding flat entry.
 */
bool strategy_ilt_offset_match(uint64_t *target_ptr, uint64_t target,
                               uint64_t current_val,
                               uint64_t import_dir_va, uint64_t import_dir_end,
                               struct import_flat *flat, int num_flat)
{
    if (current_val == 0 || target < import_dir_va || target >= import_dir_end)
        return false;
    int slot_idx = (int)((target - import_dir_va) / 8);
    if (slot_idx < 0 || slot_idx >= num_flat || flat[slot_idx].resolved_addr == 0)
        return false;
    *target_ptr = flat[slot_idx].resolved_addr;
    DEBUG("    Thunk patch (ilt-offset match): %s!%s at 0x%lx <- 0x%lx",
           flat[slot_idx].dll_name, flat[slot_idx].func_name,
           (unsigned long)target, (unsigned long)flat[slot_idx].resolved_addr);
    return true;
}

/**
 * Strategy 4: fallback by position in the flat array.
 */
bool strategy_positional(uint64_t *target_ptr, uint64_t target,
                         int thunk_idx,
                         struct import_flat *flat, int num_flat)
{
    if (thunk_idx >= num_flat || flat[thunk_idx].resolved_addr == 0)
        return false;
    *target_ptr = flat[thunk_idx].resolved_addr;
    DEBUG("    Thunk patch (pos match): %s!%s at 0x%lx <- 0x%lx",
           flat[thunk_idx].dll_name, flat[thunk_idx].func_name,
           (unsigned long)target, (unsigned long)flat[thunk_idx].resolved_addr);
    return true;
}
