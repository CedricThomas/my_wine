/*
 * dll_loader.h — DLL loading (map, relocate, register)
 *
 * Maps a DLL at a reserved base below 4GB, applies relocations,
 * registers in module list + LDR, and resolves its imports.
 */

#ifndef MY_WINE_DLL_LOADER_H
#define MY_WINE_DLL_LOADER_H

#include <stdint.h>

#include "module_list.h"

/**
 * load_dll: map a DLL, apply relocations, register in module list + LDR,
 * resolve its imports. Returns the loaded_module_t or NULL on failure.
 *
 * @param  path   full path to the DLL file
 * @param  depth  import resolution recursion depth
 * @return  loaded_module_t pointer on success, NULL on failure
 */
loaded_module_t *load_dll(const char *path, int depth);

#endif /* MY_WINE_DLL_LOADER_H */
