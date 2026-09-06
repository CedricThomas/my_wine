/*
 * pe32_entry_resolve.h -- PE32 user-entry resolution and CRT startup bypass.
 */

#ifndef MY_WINE_PE32_ENTRY_RESOLVE_H
#define MY_WINE_PE32_ENTRY_RESOLVE_H

#include <stdint.h>

#include "include/pe.h"

typedef enum {
    PE32_ENTRY_TYPE_MAIN,
    PE32_ENTRY_TYPE_WINMAIN,
    PE32_ENTRY_TYPE_WWINMAIN
} pe32_entry_type_t;

uint32_t pe32_resolve_entry_symbol(void *image_base, IMAGE_NT_HEADERS *nt,
                                   const char *path);
void pe32_patch_crt_initialized(void *image_base, IMAGE_NT_HEADERS *nt,
                                const char *path);
pe32_entry_type_t pe32_get_entry_type(void);

#endif /* MY_WINE_PE32_ENTRY_RESOLVE_H */
