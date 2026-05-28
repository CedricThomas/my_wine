/*
 * import_lookup.h -- Loader import symbol lookup helpers
 */

#ifndef MY_WINE_IMPORT_LOOKUP_H
#define MY_WINE_IMPORT_LOOKUP_H

void *resolve_loader_import(const char *dll_name, const char *func_name);

#endif /* MY_WINE_IMPORT_LOOKUP_H */
