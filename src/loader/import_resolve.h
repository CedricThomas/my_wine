/*
 * import_resolve.h — Import resolution (IAT patching)
 *
 * Resolves imports by patching IAT entries in the PE image.
 */

#ifndef MY_WINE_IMPORT_RESOLVE_H
#define MY_WINE_IMPORT_RESOLVE_H

#include "include/pe.h"

/* Pass 1 + Pass 2 import resolution */
int resolve_imports(void *base, IMAGE_NT_HEADERS64 *nt);

/* Find .text jmp-thunk whose IAT entry resolves to target_addr */
void *find_text_thunk(void *image_base, IMAGE_NT_HEADERS64 *nt,
                       IMAGE_SECTION_HEADER *sections,
                       void *target_addr);

/* Find a DLL in the search path */
int find_dll_path(const char *dll_name, char *path, size_t path_size);

#endif /* MY_WINE_IMPORT_RESOLVE_H */
