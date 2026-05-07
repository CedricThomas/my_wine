/*
 * relocations.h — PE relocation application
 *
 * Applies base relocations to a mapped PE image.
 */

#ifndef MY_WINE_RELOCATIONS_H
#define MY_WINE_RELOCATIONS_H

#include "include/pe.h"

int apply_relocations(void *base, IMAGE_NT_HEADERS64 *nt);

#endif /* MY_WINE_RELOCATIONS_H */
