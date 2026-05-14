/*
 * image_mapper.h — PE image mapping
 *
 * Function declarations for mapping PE files into memory.
 * Global loader state is consolidated in loader_state.h (g_loader).
 */

#ifndef MY_WINE_IMAGE_MAPPER_H
#define MY_WINE_IMAGE_MAPPER_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#include "include/pe.h"
#include "loader_state.h"  /* wine_loader_state_t, g_loader, accessors */

/* Accessors for the PE path */
const char *get_pe_path(void);
void set_pe_path(const char *path);

/**
 * Map a PE file at the preferred image base (uses PE's ImageBase).
 *
 * @param  path  path to the PE file
 * @param  out_dos   (optional) receives parsed DOS header
 * @param  out_nt    (optional) receives parsed NT headers
 * @param  out_nt_size (optional) receives size of parsed NT headers struct
 * @return  image base address (virtual), or NULL on failure
 */
void *map_image(const char *path,
                IMAGE_DOS_HEADER *out_dos,
                IMAGE_NT_HEADERS *out_nt,
                size_t *out_nt_size);

/**
 * Map a PE file at a specific base address.
 *
 * For PE32 images with desired_base == 0, maps at 0x00400000 (PE32 default).
 * For PE32+ images with desired_base == 0, maps at the PE's preferred ImageBase.
 *
 * @param  path         path to the PE file
 * @param  out_dos      (optional) receives parsed DOS header
 * @param  out_nt       (optional) receives parsed NT headers
 * @param  out_nt_size  (optional) receives size of parsed NT headers struct
 * @param  desired_base forced image base (0 = use PE's preferred or PE32 default)
 * @return  image base address (virtual), or NULL on failure
 */
void *map_image_at(const char *path,
                   IMAGE_DOS_HEADER *out_dos,
                   IMAGE_NT_HEADERS *out_nt,
                   size_t *out_nt_size,
                   uintptr_t desired_base);

#endif /* MY_WINE_IMAGE_MAPPER_H */
