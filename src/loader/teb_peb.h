/*
 * teb_peb.h — TEB/PEB setup and guest stack allocation
 *
 * Allocates and initializes the Thread Environment Block (TEB),
 * Process Environment Block (PEB), and guest stack.
 */

#ifndef MY_WINE_TEB_PEB_H
#define MY_WINE_TEB_PEB_H

#include <stddef.h>

#include "include/pe.h"

/* Set by setup_stack, read by entry.c for cleanup */
extern void *g_stack_base;
extern size_t g_stack_size;

void *setup_teb_peb(void);
void *setup_stack(IMAGE_NT_HEADERS *nt);

/* Fixed addresses for PE32 TEB/PEB (high in 32-bit space) */
#define TEB32_FIXED_ADDR  0x7FFDE000U
#define PEB32_FIXED_ADDR  0x7FFDF000U

/* Shared PE32 init helpers (used by pe32_entry.c and setup_teb_peb) */
void init_teb32_fields(void *teb, void *peb);
void init_peb32_fields(void *peb, void *image_base);

#endif /* MY_WINE_TEB_PEB_H */
