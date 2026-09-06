/*
 * pe32_doom95_compat.h -- PE32 Doom95 compatibility hooks.
 *
 * This isolates the PE32-specific runtime shaping that exists only to keep
 * Doom95 booting under the 32-bit loader.
 */

#ifndef MY_WINE_PE32_DOOM95_COMPAT_H
#define MY_WINE_PE32_DOOM95_COMPAT_H

#include <stddef.h>
#include <stdbool.h>

#include "include/pe.h"

bool pe32_is_doom95_path(const char *path);

void pe32_apply_doom95_runtime_compat(const char *path,
                                      void *image_base,
                                      IMAGE_NT_HEADERS *nt,
                                      char *basewad_path,
                                      size_t basewad_path_size,
                                      const char *basewad_option);

#endif /* MY_WINE_PE32_DOOM95_COMPAT_H */
