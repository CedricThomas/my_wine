/*
 * pe32_guest_launch.h -- PE32 TEB/PEB bootstrap and final guest handoff.
 */

#ifndef MY_WINE_PE32_GUEST_LAUNCH_H
#define MY_WINE_PE32_GUEST_LAUNCH_H

#include <stdint.h>

#include "include/pe.h"

void *pe32_init_peb_or_exit(void *base);
void *pe32_init_teb_or_exit(void *peb);
void pe32_prepare_dispatch_or_exit(void);
void *pe32_setup_guest_stack_or_exit(IMAGE_NT_HEADERS *nt);

__attribute__((noreturn))
void pe32_setup_fs_and_jump(void *teb, const char *pe_path,
                            uint32_t entry_abs, void *stack_top);

#endif /* MY_WINE_PE32_GUEST_LAUNCH_H */
