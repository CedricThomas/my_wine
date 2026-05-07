/*
 * dll_path.h — DLL path resolution
 *
 * Searches for DLLs in current directory, app directory, and WINE_DLL_PATH.
 */

#ifndef MY_WINE_DLL_PATH_H
#define MY_WINE_DLL_PATH_H

#include <stddef.h>

/**
 * Find the full path to a DLL.
 * Searches: current directory, app directory, WINE_DLL_PATH (semicolon-separated).
 *
 * @param  dll_name   the DLL name to find (e.g. "kernel32.dll")
 * @param  path       buffer to receive the full path
 * @param  path_size  size of the path buffer
 * @return 1 if found, 0 if not found
 */
int find_dll_path(const char *dll_name, char *path, size_t path_size);

#endif /* MY_WINE_DLL_PATH_H */
