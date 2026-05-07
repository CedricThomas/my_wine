/*
 * image_mapper.h — PE image mapping
 *
 * Globals and function declarations for mapping PE files into memory.
 */

#ifndef MY_WINE_IMAGE_MAPPER_H
#define MY_WINE_IMAGE_MAPPER_H

#include <stdint.h>
#include <stddef.h>

#include "include/pe.h"

/* Global state set by image_mapper, read by other modules */
extern void *g_image_base;
extern uintptr_t g_host_gs_base;

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
                IMAGE_NT_HEADERS64 *out_nt,
                size_t *out_nt_size);

/**
 * Map a PE file at a specific base address.
 *
 * @param  path         path to the PE file
 * @param  out_dos      (optional) receives parsed DOS header
 * @param  out_nt       (optional) receives parsed NT headers
 * @param  out_nt_size  (optional) receives size of parsed NT headers struct
 * @param  desired_base forced image base (0 = use PE's preferred ImageBase)
 * @return  image base address (virtual), or NULL on failure
 */
void *map_image_at(const char *path,
                   IMAGE_DOS_HEADER *out_dos,
                   IMAGE_NT_HEADERS64 *out_nt,
                   size_t *out_nt_size,
                   uintptr_t desired_base);

#endif /* MY_WINE_IMAGE_MAPPER_H */
