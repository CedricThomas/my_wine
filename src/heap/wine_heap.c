/*
 * wine_heap.c - Windows heap API over the selected heap backend
 *
 * Real implementations for HeapCreate, HeapAlloc, HeapFree, HeapReAlloc,
 * GetProcessHeap, HeapDestroy, and HeapSize.
 */

#define _GNU_SOURCE

#include "heap_backend.h"
#include "wine_heap.h"
#include "../msvcrt/kernel32_priv.h"
#include "../syscall/syscalls_inline.h"
#include "../loader/image_mapper.h"

#include <string.h>
#include <stdlib.h>

/* HEAP_ZERO_MEMORY flag */
#define HEAP_ZERO_MEMORY 0x00000008

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
KERNEL32_STUB
void *HeapCreate(uint32_t flOptions, uintptr_t dwInitialSize, uintptr_t dwMaximumSize)
{
    wine_heap_t *heap;

    /* Allocate the heap handle directly; the backend may not be initialized yet. */
    int map_flags = MAP_PRIVATE | MAP_ANONYMOUS;
    if (g_is_32bit_get()) {
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
KERNEL32_STUB
void *HeapAlloc(void *hHeap, uint32_t dwFlags, uintptr_t dwBytes)
{
    wine_heap_t *heap = (wine_heap_t *)hHeap;

    if (!heap || !heap->is_valid || dwBytes == 0) {
        return NULL;
    }

    void *ptr;
#ifndef MY_WINE32
    pthread_mutex_lock(&heap->mutex);
#endif
    ptr = heap_backend_malloc((size_t)dwBytes);
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
KERNEL32_STUB
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
    heap_backend_free(lpMem);
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
KERNEL32_STUB
void *HeapReAlloc(void *hHeap, uint32_t dwFlags, void *lpMem, uintptr_t dwBytes)
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
        old_size = heap_backend_usable_size(lpMem);
        ptr = heap_backend_realloc(lpMem, (size_t)dwBytes);
    } else {
        /* lpMem == NULL: treat as fresh HeapAlloc */
        ptr = heap_backend_malloc((size_t)dwBytes);
    }
#ifndef MY_WINE32
    pthread_mutex_unlock(&heap->mutex);
#endif

    if (ptr && (dwFlags & HEAP_ZERO_MEMORY)) {
        if (dwBytes > old_size) {
            /* Only zero the newly allocated portion (old data preserved) */
            memset((char *)ptr + old_size, 0, (size_t)dwBytes - old_size);
        } else if (lpMem == NULL) {
            /* Fresh allocation (lpMem was NULL): zero the entire block */
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
KERNEL32_STUB
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
KERNEL32_STUB
void *GetProcessHeap(void)
{
    return g_process_heap;
}

/*
 * HeapSize(hHeap, dwFlags, lpMem)
 * Returns the size of the allocation or -1 on error.
 */
KERNEL32_STUB
uintptr_t HeapSize(void *hHeap, uint32_t dwFlags, const void *lpMem)
{
    wine_heap_t *heap = (wine_heap_t *)hHeap;

    if (!heap || !heap->is_valid || lpMem == NULL) {
        return (uintptr_t)-1;
    }

    (void)dwFlags;
    return (uintptr_t)heap_backend_usable_size((void *)lpMem);
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
