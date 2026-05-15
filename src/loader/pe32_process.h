/*
 * pe32_process.h -- PE32 process setup helpers.
 *
 * Owned by the PE32 runtime entry layer. These helpers run before the
 * guest jump and preserve the same no-libc-after-sensitive-transition
 * expectations as pe32_entry.c.
 */

#ifndef MY_WINE_PE32_PROCESS_H
#define MY_WINE_PE32_PROCESS_H

#include <stdint.h>

#include "include/pe.h"

extern void *g_argv_page;

void ensure_argv_setup(const char *pe_path);
uint32_t pe32_argv_ptr(void);
uint32_t pe32_envp_ptr(void);

void wire_peb32_fields(void *peb, void *image_base,
                       IMAGE_NT_HEADERS *nt, const char *pe_path);
void seed_pe32_bss_vars(void *base, IMAGE_NT_HEADERS *nt);

#endif /* MY_WINE_PE32_PROCESS_H */
