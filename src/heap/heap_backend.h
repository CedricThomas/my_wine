#ifndef MY_WINE_HEAP_BACKEND_H
#define MY_WINE_HEAP_BACKEND_H

#include <stddef.h>

void *heap_backend_malloc(size_t size);
void *heap_backend_calloc(size_t count, size_t size);
void *heap_backend_realloc(void *ptr, size_t size);
void heap_backend_free(void *ptr);
void *heap_backend_aligned_alloc(size_t align, size_t len);
size_t heap_backend_usable_size(void *ptr);

#endif /* MY_WINE_HEAP_BACKEND_H */
