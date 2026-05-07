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
void *setup_stack(IMAGE_OPTIONAL_HEADER64 *opt);

#endif /* MY_WINE_TEB_PEB_H */
