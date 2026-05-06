/*
 * wine_heap.c — Heap management with dlmalloc backend
 *
 * Real implementations for HeapCreate, HeapAlloc, HeapFree, HeapReAlloc,
 * GetProcessHeap, HeapDestroy, HeapSize backed by dlmalloc.
 */

#define _GNU_SOURCE

#include "wine_heap.h"
#include "../msvcrt/kernel32_priv.h"
#include "../syscall/syscalls_inline.h"

#include <string.h>
#include <stdlib.h>

/* HEAP_ZERO_MEMORY flag */
#define HEAP_ZERO_MEMORY 0x00000008

/* Global process heap */
void *g_process_heap = NULL;

/* wine_heap_t: wrapper around a named heap */
typedef struct wine_heap {
    pthread_mutex_t mutex;
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

    /* Allocate the heap structure via mmap (not dlmalloc — it doesn't exist yet) */
    void *mem = INLINE_SYSCALL_MMAP(NULL, sizeof(wine_heap_t),
                                     PROT_READ | PROT_WRITE,
                                     MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (mem == (void *)-1 || mem == NULL) {
        return NULL;
    }

    heap = (wine_heap_t *)mem;
    pthread_mutex_init(&heap->mutex, NULL);
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
    pthread_mutex_lock(&heap->mutex);
    ptr = dlmalloc((size_t)dwBytes);
    pthread_mutex_unlock(&heap->mutex);

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

    pthread_mutex_lock(&heap->mutex);
    dlfree(lpMem);
    pthread_mutex_unlock(&heap->mutex);

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

    pthread_mutex_lock(&heap->mutex);
    if (lpMem) {
        old_size = dlmalloc_usable_size(lpMem);
        ptr = dlrealloc(lpMem, (size_t)dwBytes);
    } else {
        /* lpMem == NULL → treat as fresh HeapAlloc */
        ptr = dlmalloc((size_t)dwBytes);
    }
    pthread_mutex_unlock(&heap->mutex);

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
    pthread_mutex_destroy(&heap->mutex);

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
    return (uint64_t)dlmalloc_usable_size(lpMem);
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
