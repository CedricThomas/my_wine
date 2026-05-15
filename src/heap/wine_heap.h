#ifndef WINE_HEAP_H
#define WINE_HEAP_H

#include <stdint.h>
#include <pthread.h>
#include <stddef.h>

/* Process heap */
void *init_process_heap(void);
extern void *g_process_heap;

#endif /* WINE_HEAP_H */
