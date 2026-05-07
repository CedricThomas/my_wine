/*
 * ordinal_table.h — Ordinal import name lookup
 *
 * Static lookup table mapping (DLL name, ordinal) → function name.
 */

#ifndef MY_WINE_ORDINAL_TABLE_H
#define MY_WINE_ORDINAL_TABLE_H

#include <stdint.h>

const char *ordinal_lookup(const char *dll_name, uint16_t ordinal);

#endif /* MY_WINE_ORDINAL_TABLE_H */
