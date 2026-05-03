/*
 * import_resolver.c — PE import table resolution
 *
 * Maintains the import_entry_t table mapping DLL/function names
 * to our stub implementations. Resolves imports by patching IAT
 * entries in the PE image.
 */

#define _GNU_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <search.h>   // for bsearch, qsort
#include <stdbool.h>

#include "include/pe.h"
#include "include/ntdll.h"
#include "include/kernel32.h"
#include "include/msvcrt.h"
#include "loader_priv.h"

/* Name→address table for NT, kernel32 and msvcrt functions */
import_entry_t import_table[] = {
    /* ntdll functions (via syscall thunks) */
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
    /* kernel32 functions */
    { "kernel32.dll", "GetStdHandle", (void*)GetStdHandle },
    { "kernel32.dll", "WriteFile", (void*)WriteFile },
    { "kernel32.dll", "ReadFile", (void*)ReadFile },
    { "kernel32.dll", "ExitProcess", (void*)ExitProcess },
    { "kernel32.dll", "GetProcAddress", (void*)GetProcAddress },
    { "kernel32.dll", "LoadLibraryA", (void*)LoadLibraryA },
    { "kernel32.dll", "GetModuleHandleA", (void*)GetModuleHandleA },
    { "kernel32.dll", "lstrlenA", (void*)lstrlenA },
    { "kernel32.dll", "DeleteCriticalSection", (void*)DeleteCriticalSection },
    { "kernel32.dll", "EnterCriticalSection", (void*)EnterCriticalSection },
    { "kernel32.dll", "GetLastError", (void*)GetLastError },
    { "kernel32.dll", "GetStartupInfoA", (void*)GetStartupInfoA },
    { "kernel32.dll", "InitializeCriticalSection", (void*)InitializeCriticalSection },
    { "kernel32.dll", "LeaveCriticalSection", (void*)LeaveCriticalSection },
    { "kernel32.dll", "SetUnhandledExceptionFilter", (void*)SetUnhandledExceptionFilter },
    { "kernel32.dll", "Sleep", (void*)Sleep },
    { "kernel32.dll", "TlsGetValue", (void*)TlsGetValue },
    { "kernel32.dll", "VirtualProtect", (void*)VirtualProtect },
    { "kernel32.dll", "VirtualQuery", (void*)VirtualQuery },
    { "ntdll.dll", "__C_specific_handler", (void*)__C_specific_handler },
    /* msvcrt functions (statically known) */
    { "msvcrt.dll", "__getmainargs", (void*)__getmainargs },
    { "msvcrt.dll", "__initenv", (void*)__initenv },
    { "msvcrt.dll", "__iob_func", (void*)__iob_func },
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
    /* Dynamic entries - filled by init_msvcrt_imports() */
    { "msvcrt.dll", "abort", NULL },
    { "msvcrt.dll", "calloc", NULL },
    { "msvcrt.dll", "exit", NULL },
    { "msvcrt.dll", "fprintf", NULL },
    { "msvcrt.dll", "free", NULL },
    { "msvcrt.dll", "fwrite", NULL },
    { "msvcrt.dll", "malloc", NULL },
    { "msvcrt.dll", "memcpy", NULL },
    { "msvcrt.dll", "signal", NULL },
    { "msvcrt.dll", "strlen", NULL },
    { "msvcrt.dll", "strncmp", NULL },
    { "msvcrt.dll", "vfprintf", NULL },
    { NULL, NULL, NULL }
};

static void set_import(const char *name, void *address)
{
    for (int i = 0; import_table[i].name != NULL; i++) {
        if (strcmp(import_table[i].name, name) == 0) {
            import_table[i].address = address;
            return;
        }
    }
    fprintf(stderr, "ERROR: set_import: symbol '%s' not found\n", name);
}

/* ── Binary-search helpers (t3.4) ─────────────────────────── */

static int import_entry_cmp(const void *a, const void *b)
{
    return strcmp(((const import_entry_t *)a)->name,
                  ((const import_entry_t *)b)->name);
}

static int import_cmp_by_name(const void *key, const void *elem)
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
 * Fill dynamic msvcrt import entries (abort, malloc, etc.) with
 * pointers to our internal implementations.
 */
void init_msvcrt_imports(void)
{
    set_import("abort",    __msvcrt_abort);
    set_import("calloc",   __msvcrt_calloc);
    set_import("exit",     __msvcrt_exit);
    set_import("fprintf",  __msvcrt_fprintf);
    set_import("free",     __msvcrt_free);
    set_import("fwrite",   __msvcrt_fwrite);
    set_import("malloc",   __msvcrt_malloc);
    set_import("memcpy",   __msvcrt_memcpy);
    set_import("signal",   __msvcrt_signal);
    set_import("strlen",   __msvcrt_strlen);
    set_import("strncmp",  __msvcrt_strncmp);
    set_import("vfprintf", __msvcrt_vfprintf);
}

/**
 * Find the .text jmp-thunk address whose IAT entry resolves to target_addr.
 * Scans all "ff 25 disp32" (jmp *disp(%rip)) instructions in .text and
 * checks if the dereferenced IAT pointer equals target_addr.
 * Returns the absolute address of the thunk instruction, or NULL.
 */
void *find_text_thunk(void *image_base, IMAGE_NT_HEADERS64 *nt,
                       IMAGE_SECTION_HEADER *sections,
                       void *target_addr)
{
    /* Locate .text section */
    uint64_t text_start = 0, text_end = 0;
    for (uint16_t i = 0; i < nt->FileHeader.NumberOfSections; i++) {
        if (memcmp(sections[i].Name, ".text", 5) == 0) {
            text_start = sections[i].VirtualAddress;
            text_end   = text_start + sections[i].Misc.VirtualSize;
            if (text_end < text_start || sections[i].SizeOfRawData > sections[i].Misc.VirtualSize)
                text_end = text_start + sections[i].SizeOfRawData;
            break;
        }
    }
    if (text_start == 0) return NULL;

    uint8_t *text_base = (uint8_t *)image_base + text_start;
    uint64_t text_size = text_end - text_start;
    uint64_t target_val = (uint64_t)(uintptr_t)target_addr;

    for (uint64_t off = 0; off + 6 <= text_size; off++) {
        if (text_base[off] == 0xff && text_base[off + 1] == 0x25) {
            int32_t disp = *(int32_t *)(text_base + off + 2);
            uint64_t instr_addr = text_start + off;
            uint64_t target_rva = instr_addr + 6 + disp;
            uint64_t *target_ptr = (uint64_t *)((char *)image_base + target_rva);
            if (*target_ptr == target_val) {
                return (void *)((char *)image_base + instr_addr);
            }
        }
    }
    return NULL;
}

static void *resolve_import(const char *dll_name, const char *func_name)
{
    size_t count = sizeof(import_table) / sizeof(import_entry_t) - 1;
    import_entry_t *entry = bsearch(func_name, import_table,
                                     count, sizeof(import_entry_t), import_cmp_by_name);
    if (entry == NULL) {
        fprintf(stderr, "  ERROR: unresolved import: %s!%s\n", dll_name, func_name);
        return NULL;
    }
    if (entry->address == NULL) {
        fprintf(stderr, "  ERROR: import %s!%s has NULL address (not initialized)\n",
                dll_name, func_name);
        return NULL;
    }
    if (entry->dll_name && strcmp(entry->dll_name, dll_name) != 0) {
        fprintf(stderr, "  WARNING: %s found in %s but requested from %s\n",
                func_name, entry->dll_name, dll_name);
        return NULL;
    }
    return entry->address;
}

/**
 * Resolve all imports in the PE image.
 *
 * Pass 1: resolve and write to descriptor's FirstThunk (IAT).
 * Pass 2: patch thunk IAT targets found by scanning .text
 * (for non-standard import layouts where .text jmp thunks reference
 *  addresses that differ from the descriptor's FirstThunk).
 */
int resolve_imports(void *base, IMAGE_NT_HEADERS64 *nt)
{

    IMAGE_OPTIONAL_HEADER64 *opt = &nt->OptionalHeader;

    /* Get import directory */

    if (opt->DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT].Size == 0) {
        printf("No imports to resolve\n");
        return 0;
    }

    uint64_t import_rva = opt->DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT].VirtualAddress;

    IMAGE_IMPORT_DESCRIPTOR *desc = (IMAGE_IMPORT_DESCRIPTOR *)((char *)base + import_rva);


    printf("Resolving imports:\n");
    fflush(stdout);

    /* ── Pass 1: resolve and write to descriptor's FirstThunk (IAT) ──── */

    IMAGE_IMPORT_DESCRIPTOR *desc_start = desc;
    while (desc->Name != 0) {
        const char *dll_name = (const char *)((char *)base + desc->Name);

        printf("  DLL: %s\n", dll_name);

        /* Get the original thunk table (with function names/ordinals) */
        IMAGE_THUNK_DATA64 *orig_thunks = (IMAGE_THUNK_DATA64 *)((char *)base + desc->u1.OriginalFirstThunk);
        IMAGE_THUNK_DATA64 *iath = (IMAGE_THUNK_DATA64 *)((char *)base + desc->FirstThunk);


        for (int i = 0; orig_thunks[i].AddressOfData != 0; i++) {
            void *addr = NULL;

            if (orig_thunks[i].AddressOfData & 0x8000000000000000ULL) {
                /* Ordinal import (high bit set) */
                uint64_t ordinal = orig_thunks[i].AddressOfData & 0xFFFF;
                fprintf(stderr, "  WARNING: ordinal import %lu not supported\n", ordinal);
                continue;
            } else {
                /* Name import */
                IMAGE_IMPORT_BY_NAME *imp_name = (IMAGE_IMPORT_BY_NAME *)((char *)base + orig_thunks[i].AddressOfData);
                addr = resolve_import(dll_name, (const char *)imp_name->Name);
            }

            if (addr != NULL) {
                printf("    Resolved %s -> %p\n",
                       orig_thunks[i].AddressOfData & 0x8000000000000000ULL ?
                       "<ordinal>" :
                       ((IMAGE_IMPORT_BY_NAME *)((char *)base + orig_thunks[i].AddressOfData))->Name,
                       addr);
                iath[i].AddressOfData = (uint64_t)(uintptr_t)addr;
            } else {
                fprintf(stderr, "    FAILED to resolve import at index %d\n", i);
            }
        }

        desc++;
    }

    /* ── Pass 2: patch thunk IAT targets found by scanning .text ──── */
    /* The PE has non-standard import layout: the jmp thunks in .text read from
     * addresses (e.g., 0x878c-0x88cc) that differ from the descriptor's FirstThunk
     * (e.g., 0x80bc-0x8204). These thunk targets are in a zero-padded region of
     * .idata that was never populated from the file. We need to populate them.
     *
     * Strategy: scan .text for all "jmp *disp32(%rip)" instructions (ff 25),
     * collect unique targets, sort them. These correspond to the ILT entries in
     * DLL order. Match by position: the nth sorted target gets the nth resolved
     * address from the concatenated IAT entries. */

    /* Find .text section — compute section headers from the actual image, not
     * the local nt copy (which doesn't have sections past the optional header). */
    const IMAGE_DOS_HEADER *img_dos = (const IMAGE_DOS_HEADER *)base;
    uint32_t pe_off = img_dos->e_lfanew;
    uint32_t sec_off = pe_off + sizeof(uint32_t) + sizeof(IMAGE_FILE_HEADER) +
                       nt->FileHeader.SizeOfOptionalHeader;
    IMAGE_SECTION_HEADER *sections = (IMAGE_SECTION_HEADER *)((char *)base + sec_off);
    uint16_t num_sections = nt->FileHeader.NumberOfSections;

    uint64_t text_start = 0, text_end = 0;
    for (uint16_t i = 0; i < num_sections; i++) {
        if (memcmp(sections[i].Name, ".text", 5) == 0) {
            text_start = sections[i].VirtualAddress;
            text_end   = text_start + sections[i].Misc.VirtualSize;
            if (text_end < text_start || sections[i].SizeOfRawData > sections[i].Misc.VirtualSize)
                text_end = text_start + sections[i].SizeOfRawData;
            break;
        }
    }

    if (text_start == 0) {
        fprintf(stderr, "WARNING: .text section not found, skipping thunk scan\n");
    } else {
        uint8_t *text_base = (uint8_t *)base + text_start;
        uint64_t text_size = text_end - text_start;

        /* Step 1: scan .text for all ff 25 xx xx xx xx (jmp *disp32(%rip)),
         * collect unique target addresses */
        uint64_t thunk_targets[256];
        int num_targets = 0;

        for (uint64_t off = 0; off + 6 <= text_size; off++) {
            if (text_base[off] == 0xff && text_base[off + 1] == 0x25) {
                int32_t disp = *(int32_t *)(text_base + off + 2);
                uint64_t instr_addr = text_start + off;
                uint64_t target = instr_addr + 6 + disp;

                /* Deduplicate */
                int dup = 0;
                for (int t = 0; t < num_targets; t++) {
                    if (thunk_targets[t] == target) { dup = 1; break; }
                }
                if (!dup && num_targets < 256) {
                    thunk_targets[num_targets++] = target;
                }
            }
        }

        /* Step 2: sort targets by address */
        for (int i = 0; i < num_targets - 1; i++) {
            for (int j = i + 1; j < num_targets; j++) {
                if (thunk_targets[j] < thunk_targets[i]) {
                    uint64_t tmp = thunk_targets[i];
                    thunk_targets[i] = thunk_targets[j];
                    thunk_targets[j] = tmp;
                }
            }
        }

        printf("  Found %d thunk targets in .text (range 0x%lx-0x%lx)\n",
               num_targets,
               num_targets > 0 ? (unsigned long)thunk_targets[0] : 0,
               num_targets > 0 ? (unsigned long)thunk_targets[num_targets - 1] + 7 : 0);

        /* Step 3: build flat array of (ILT_value, resolved_addr, func_name) from
         * all import descriptors in DLL order */
        struct import_flat {
            uint64_t ilt_value;       /* OriginalFirstThunk[i].AddressOfData */
            uint64_t resolved_addr;   /* FirstThunk[i].AddressOfData (from pass 1) */
            const char *dll_name;
            const char *func_name;
        };
        struct import_flat flat[256];
        int num_flat = 0;

        desc = desc_start;
        while (desc->Name != 0 && num_flat < 256) {
            const char *dll_name = (const char *)((char *)base + desc->Name);
            IMAGE_THUNK_DATA64 *orig_thunks = (IMAGE_THUNK_DATA64 *)((char *)base + desc->u1.OriginalFirstThunk);
            IMAGE_THUNK_DATA64 *iath = (IMAGE_THUNK_DATA64 *)((char *)base + desc->FirstThunk);

            for (int i = 0; orig_thunks[i].AddressOfData != 0 && num_flat < 256; i++) {
                flat[num_flat].ilt_value = orig_thunks[i].AddressOfData;
                flat[num_flat].resolved_addr = iath[i].AddressOfData;
                flat[num_flat].dll_name = dll_name;
                if (orig_thunks[i].AddressOfData & 0x8000000000000000ULL) {
                    flat[num_flat].func_name = "<ordinal>";
                } else {
                    IMAGE_IMPORT_BY_NAME *imp_name = (IMAGE_IMPORT_BY_NAME *)((char *)base + orig_thunks[i].AddressOfData);
                    flat[num_flat].func_name = (const char *)imp_name->Name;
                }
                num_flat++;
            }
            desc++;
        }

        printf("  Flat import array: %d entries from %d descriptors\n",
               num_flat, (int)(desc - desc_start));

        /* Step 4: match thunk targets to flat entries
         * The thunk IAT may contain:
         * (a) ILT RVAs (if from file data that was copied as OriginalFirstThunk)
         * (b) Already-resolved addresses (if the target overlaps with FirstThunk
         *     that pass 1 populated)
         * (c) Garbage/random file data
         *
         * Strategy: try matching by resolved_addr first (handles overlap case),
         * then by ILT value, then by position. */
        int matched = 0;
        for (int t = 0; t < num_targets; t++) {
            uint64_t target = thunk_targets[t];
            uint64_t *target_ptr = (uint64_t *)((char *)base + target);
            uint64_t current_val = *target_ptr;

            int did_match = 0;

            /* Try matching by resolved address (target overlaps with IAT from pass 1) */
            for (int f = 0; f < num_flat; f++) {
                if (flat[f].resolved_addr == current_val) {
                    /* Already has the correct resolved address - no write needed */
                    matched++;
                    did_match = 1;
                    break;
                }
            }

            if (!did_match && current_val != 0) {
                /* Try matching by ILT RVA */
                for (int f = 0; f < num_flat; f++) {
                    if (flat[f].ilt_value == current_val && flat[f].resolved_addr != 0) {
                        *target_ptr = flat[f].resolved_addr;
                        printf("    Thunk patch (ilt match): %s!%s at 0x%lx <- 0x%lx\n",
                               flat[f].dll_name, flat[f].func_name,
                               (unsigned long)target, (unsigned long)flat[f].resolved_addr);
                        matched++;
                        did_match = 1;
                        break;
                    }
                }
            }

            if (!did_match) {
                /* Try ILT RVA offset matching: if the thunk target address falls
                 * within the import data directory (ILT region), compute which
                 * slot it corresponds to by offset / 8 and match against the
                 * flat array at that index. */
                if (current_val != 0) {
                    uint64_t import_dir_va = opt->DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT].VirtualAddress;
                    uint64_t import_dir_end = import_dir_va + opt->DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT].Size;
                    if (target >= import_dir_va && target < import_dir_end) {
                        int slot_idx = (int)((target - import_dir_va) / 8);
                        if (slot_idx >= 0 && slot_idx < num_flat &&
                            flat[slot_idx].resolved_addr != 0) {
                            *target_ptr = flat[slot_idx].resolved_addr;
                            printf("    Thunk patch (ilt-offset match): %s!%s at 0x%lx <- 0x%lx\n",
                                   flat[slot_idx].dll_name, flat[slot_idx].func_name,
                                   (unsigned long)target, (unsigned long)flat[slot_idx].resolved_addr);
                            matched++;
                            did_match = 1;
                        }
                    }
                }
            }

            if (!did_match) {
                /* LAST RESORT: match by position (nth target <- nth flat entry).
                 * The linker guarantees thunk targets and ILT entries appear in
                 * the same DLL+function order, so positional matching works as
                 * an absolute fallback. */
                if (t < num_flat && flat[t].resolved_addr != 0) {
                    *target_ptr = flat[t].resolved_addr;
                    printf("    Thunk patch (pos match): %s!%s at 0x%lx <- 0x%lx\n",
                           flat[t].dll_name, flat[t].func_name,
                           (unsigned long)target, (unsigned long)flat[t].resolved_addr);
                    matched++;
                }
            }
        }

        printf("  Thunk IAT patched: %d/%d targets resolved\n", matched, num_targets);
    }

    return 0;
}
