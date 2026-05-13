/*
 * wine_heap.c — Heap management with musl malloc backend
 *
 * Real implementations for HeapCreate, HeapAlloc, HeapFree, HeapReAlloc,
 * GetProcessHeap, HeapDestroy, HeapSize backed by musl oldmalloc.
 */

#define _GNU_SOURCE

#include "wine_heap.h"
#include "../msvcrt/kernel32_priv.h"
#include "../syscall/syscalls_inline.h"
#include "../loader/image_mapper.h"

#include <string.h>
#include <stdlib.h>

/* HEAP_ZERO_MEMORY flag */
#define HEAP_ZERO_MEMORY 0x00000008

/* musl backend */
extern void *musl_malloc(size_t);
extern void  musl_free(void *);
extern void *musl_realloc(void *, size_t);
extern size_t musl_malloc_usable_size(void *);

/* Global process heap */
void *g_process_heap = NULL;

/* wine_heap_t: wrapper around a named heap */
typedef struct wine_heap {
#ifndef MY_WINE32
    pthread_mutex_t mutex;
#endif
    int is_valid;  /* flag to validate heap handles */
} wine_heap_t;

/*
 * HeapCreate(flOptions, dwInitialSize, dwMaximumSize)
 * Returns a heap handle or NULL on failure.
 */
WINE_STUB
void *HeapCreate(uint32_t flOptions, uint64_t dwInitialSize, uint64_t dwMaximumSize)
{
    wine_heap_t *heap;

    /* Allocate the heap structure via mmap (not musl — it doesn't exist yet) */
    int map_flags = MAP_PRIVATE | MAP_ANONYMOUS;
    if (g_is_32bit) {
        map_flags |= MAP_32BIT;  /* Ensure heap is below 4GB for PE32 */
    }
    void *mem = INLINE_SYSCALL_MMAP(NULL, sizeof(wine_heap_t),
                                     PROT_READ | PROT_WRITE,
                                     map_flags, -1, 0);
    if (mem == (void *)-1 || mem == NULL) {
        return NULL;
    }

    heap = (wine_heap_t *)mem;
#ifndef MY_WINE32
    pthread_mutex_init(&heap->mutex, NULL);
#endif
    heap->is_valid = 1;

    (void)flOptions;
    (void)dwInitialSize;
    (void)dwMaximumSize;

    return (void *)heap;
}

/*
 * HeapAlloc(hHeap, dwFlags, dwBytes)
 * Returns pointer to allocated memory or NULL.
 */
WINE_STUB
void *HeapAlloc(void *hHeap, uint32_t dwFlags, uint64_t dwBytes)
{
    wine_heap_t *heap = (wine_heap_t *)hHeap;

    if (!heap || !heap->is_valid || dwBytes == 0) {
        return NULL;
    }

    void *ptr;
#ifndef MY_WINE32
    pthread_mutex_lock(&heap->mutex);
#endif
    ptr = musl_malloc((size_t)dwBytes);
#ifndef MY_WINE32
    pthread_mutex_unlock(&heap->mutex);
#endif

    if (ptr && (dwFlags & HEAP_ZERO_MEMORY)) {
        memset(ptr, 0, (size_t)dwBytes);
    }

    return ptr;
}

/*
 * HeapFree(hHeap, dwFlags, lpMem)
 * Returns non-zero on success, zero on failure.
 */
WINE_STUB
int HeapFree(void *hHeap, uint32_t dwFlags, void *lpMem)
{
    wine_heap_t *heap = (wine_heap_t *)hHeap;

    if (!heap || !heap->is_valid) {
        return 0;
    }

    if (lpMem == NULL) {
        return 1;  /* Windows: HeapFree(heap, 0, NULL) is a valid no-op */
    }

#ifndef MY_WINE32
    pthread_mutex_lock(&heap->mutex);
#endif
    musl_free(lpMem);
#ifndef MY_WINE32
    pthread_mutex_unlock(&heap->mutex);
#endif

    (void)dwFlags;
    return 1;
}

/*
 * HeapReAlloc(hHeap, dwFlags, lpMem, dwBytes)
 * Returns new pointer (may be same or different address).
 */
WINE_STUB
void *HeapReAlloc(void *hHeap, uint32_t dwFlags, void *lpMem, uint64_t dwBytes)
{
    wine_heap_t *heap = (wine_heap_t *)hHeap;

    if (!heap || !heap->is_valid) {
        return NULL;
    }

    void *ptr;
    size_t old_size = 0;

#ifndef MY_WINE32
    pthread_mutex_lock(&heap->mutex);
#endif
    if (lpMem) {
        old_size = musl_malloc_usable_size(lpMem);
        ptr = musl_realloc(lpMem, (size_t)dwBytes);
    } else {
        /* lpMem == NULL → treat as fresh HeapAlloc */
        ptr = musl_malloc((size_t)dwBytes);
    }
#ifndef MY_WINE32
    pthread_mutex_unlock(&heap->mutex);
#endif

    if (ptr && (dwFlags & HEAP_ZERO_MEMORY)) {
        if (dwBytes > old_size) {
            /* Only zero the newly allocated portion (old data preserved) */
            memset((char *)ptr + old_size, 0, (size_t)dwBytes - old_size);
        } else if (lpMem == NULL) {
            /* Fresh allocation (lpMem was NULL) — zero the entire block */
            memset(ptr, 0, (size_t)dwBytes);
        }
        /* If dwBytes <= old_size and lpMem != NULL: nothing to zero (data preserved) */
    }

    return ptr;
}

/*
 * HeapDestroy(hHeap)
 * Returns non-zero on success, zero on failure.
 */
WINE_STUB
int HeapDestroy(void *hHeap)
{
    wine_heap_t *heap = (wine_heap_t *)hHeap;

    if (!heap || !heap->is_valid) {
        return 0;
    }

    heap->is_valid = 0;
#ifndef MY_WINE32
    pthread_mutex_destroy(&heap->mutex);
#endif

    if (hHeap == g_process_heap) {
        g_process_heap = NULL;
    }

    /* Unmap the heap struct */
    INLINE_SYSCALL_MUNMAP(hHeap, sizeof(wine_heap_t));

    return 1;
}

/*
 * GetProcessHeap()
 * Returns the process default heap.
 */
WINE_STUB
void *GetProcessHeap(void)
{
    return g_process_heap;
}

/*
 * HeapSize(hHeap, dwFlags, lpMem)
 * Returns the size of the allocation or -1 on error.
 */
WINE_STUB
uint64_t HeapSize(void *hHeap, uint32_t dwFlags, const void *lpMem)
{
    wine_heap_t *heap = (wine_heap_t *)hHeap;

    if (!heap || !heap->is_valid || lpMem == NULL) {
        return (uint64_t)-1;
    }

    (void)dwFlags;
    return (uint64_t)musl_malloc_usable_size((void *)lpMem);
}

/*
 * init_process_heap()
 * Called early in the loader to create the default process heap.
 */
void *init_process_heap(void)
{
    if (g_process_heap != NULL) {
        return g_process_heap;
    }
    g_process_heap = HeapCreate(0, 65536, 0);
    return g_process_heap;
}
