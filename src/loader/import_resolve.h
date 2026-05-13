/*
 * import_resolve.h — Import resolution (IAT patching)
 *
 * Resolves imports by patching IAT entries in the PE image.
 */

#ifndef MY_WINE_IMPORT_RESOLVE_H
#define MY_WINE_IMPORT_RESOLVE_H

#include "include/pe.h"
#include "module_list.h"

/* Pass 1 + Pass 2 import resolution */
int resolve_imports(void *base, IMAGE_NT_HEADERS *nt);

/* Find .text jmp-thunk whose IAT entry resolves to target_addr */
void *find_text_thunk(void *image_base, IMAGE_NT_HEADERS *nt,
                       IMAGE_SECTION_HEADER *sections,
                       void *target_addr);

/* find_dll_path moved to dll_path.h, load_dll moved to dll_loader.h */

/* Resolve imports for a specific loaded module (recursive) */
int resolve_module_imports(loaded_module_t *mod, int depth);

#endif /* MY_WINE_IMPORT_RESOLVE_H */
