/*
 * import_init.h — msvcrt dynamic import initialization
 *
 * Fills NULL entries in import_table for dynamically-resolved msvcrt symbols.
 */

#ifndef MY_WINE_IMPORT_INIT_H
#define MY_WINE_IMPORT_INIT_H

void init_msvcrt_imports(void);

#endif /* MY_WINE_IMPORT_INIT_H */
