#ifndef MY_WINE_RESOURCE_WIN32_H
#define MY_WINE_RESOURCE_WIN32_H

#include <stdint.h>

void *wine_resource_find(void *hModule, const char *type, const char *name,
                         uint32_t *out_size);
const void *wine_resource_lock(void *resource_handle);
uint32_t wine_resource_size(void *resource_handle);

#endif
