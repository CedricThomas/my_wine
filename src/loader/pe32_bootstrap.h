/*
 * pe32_bootstrap.h -- PE32 bootstrap and early runtime setup helpers.
 */

#ifndef MY_WINE_PE32_BOOTSTRAP_H
#define MY_WINE_PE32_BOOTSTRAP_H

#include <stdint.h>
#include <stddef.h>

#include "include/pe.h"

const char *pe32_resolve_path_or_null(int argc, char **argv);
void pe32_init_runtime_debug_level(void);
void pe32_seed_command_line(const char *pe_path);
void *pe32_map_image_or_exit(const char *path, IMAGE_NT_HEADERS *out_nt);
void pe32_activate_crt(void *image_base, IMAGE_NT_HEADERS *nt,
                       const char *pe_path);
void pe32_resolve_imports_or_exit(void *image_base, IMAGE_NT_HEADERS *nt);
void pe32_debug_verify_import_state(void *image_base, IMAGE_NT_HEADERS *nt);
void pe32_seed_crt_bss(void *image_base, IMAGE_NT_HEADERS *nt);

#endif /* MY_WINE_PE32_BOOTSTRAP_H */
