/*
 * dll_loader.h — DLL loading and module import resolution
 *
 * Handles dynamic DLL loading (mapping, relocation, registration, import
 * resolution) and resolves imports for already-loaded modules.
 *
 * Extracted from import_resolve.c — moved from cross-cutting declarations.
 */

#ifndef MY_WINE_DLL_LOADER_H
#define MY_WINE_DLL_LOADER_H

#include <stdint.h>

#include "module_list.h"

#define DLL_ALLOC_BASE 0x60000000  /* DLL base allocator: maps DLLs below 4GB to avoid GCC ms_abi truncation bug */

/* DLL base allocator: maps DLLs below 4GB to avoid GCC ms_abi truncation bug.
 * Uses atomic operations for allocation — still not fully thread-safe (mmap
 * and module registration are separate steps), but prevents overlapping bases.
 */
extern volatile uintptr_t g_dll_base_next;

/* Resolve imports for a dynamically loaded module (recursive).
 * Calls find_dll_path, load_dll, resolve_imports, and parse_export_table. */
int resolve_module_imports(loaded_module_t *mod, int depth);

/* Map a DLL, apply relocations, register in module list + LDR,
 * resolve its imports. Returns the loaded_module_t or NULL on failure.
 * depth tracks recursion to prevent infinite loops. */
loaded_module_t *load_dll(const char *path, int depth);

#endif /* MY_WINE_DLL_LOADER_H */
