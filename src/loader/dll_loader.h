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

#include "module_list.h"

/* Resolve imports for a dynamically loaded module (recursive).
 * Calls find_dll_path, load_dll, resolve_imports, and parse_export_table. */
int resolve_module_imports(loaded_module_t *mod, int depth);

/* Map a DLL, apply relocations, register in module list + LDR,
 * resolve its imports. Returns the loaded_module_t or NULL on failure.
 * depth tracks recursion to prevent infinite loops. */
loaded_module_t *load_dll(const char *path, int depth);

#endif /* MY_WINE_DLL_LOADER_H */
