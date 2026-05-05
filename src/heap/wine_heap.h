#ifndef WINE_HEAP_H
#define WINE_HEAP_H

#include <stdint.h>
#include <pthread.h>
#include <stddef.h>

/* External dlmalloc interface — defined in dlmalloc.c */
void *dlmalloc(size_t bytes);
void dlfree(void *mem);
void *dlrealloc(void *mem, size_t new_size);
size_t dlmalloc_usable_size(const void *mem);

/* Process heap */
void *init_process_heap(void);
extern void *g_process_heap;

#endif /* WINE_HEAP_H */
