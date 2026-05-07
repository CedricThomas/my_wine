/*
 * gs_base.h — GS base set/get helpers
 *
 * Provides arch_prctl and FSGSBASE fallback for setting the
 * GS segment base.
 */

#ifndef MY_WINE_GS_BASE_H
#define MY_WINE_GS_BASE_H

int set_gs_base(void *addr);
void *get_gs_base(void);

#endif /* MY_WINE_GS_BASE_H */
