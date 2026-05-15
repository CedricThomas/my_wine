/*
 * pe32_mmap_heap_backend.c — Minimal malloc/free/realloc for 32-bit builds.
 *
 * PE32-only alternative to the PE32+ musl backend. Each allocation is its own
 * mmap region.
 * Not a real allocator (no pooling, no fragmentation management) but sufficient
 * for the PE32 child which allocates very few small objects.
 */

#if defined(__i386__)

#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include "heap_backend.h"
#include "../syscall/syscalls_inline.h"

/* Minimum allocation size (one page) */
#define MIN_ALLOC 4096

/*
 * heap_backend_malloc — allocate via mmap (MAP_32BIT to stay in 32-bit space).
 * Each allocation includes a 4-byte size header before the user pointer.
 */
void *heap_backend_malloc(size_t size)
{
    if (size == 0) return NULL;
    /* Align up to page boundary, add 4 bytes for size header */
    size_t alloc = (size + 7) & ~(size_t)7;
    if (alloc < MIN_ALLOC) alloc = MIN_ALLOC;

    void *mem = INLINE_SYSCALL_MMAP(
        NULL, alloc + 4,
        PROT_READ | PROT_WRITE,
        MAP_PRIVATE | MAP_ANONYMOUS | MAP_32BIT,
        -1, 0);
    if (mem == MAP_FAILED || mem == NULL) return NULL;
    *(uint32_t *)mem = (uint32_t)alloc;
    return (void *)((uint8_t *)mem + 4);
}

/*
 * heap_backend_free — munmap the allocation (header + payload).
 */
void heap_backend_free(void *ptr)
{
    if (!ptr) return;
    uint8_t *base = (uint8_t *)ptr - 4;
    size_t alloc = *(uint32_t *)base + 4;
    INLINE_SYSCALL_MUNMAP(base, alloc);
}

/*
 * heap_backend_realloc — realloc via copy (new mmap, memcpy, free old).
 */
void *heap_backend_realloc(void *ptr, size_t new_size)
{
    if (!ptr) return heap_backend_malloc(new_size);
    if (new_size == 0) { heap_backend_free(ptr); return NULL; }

    uint8_t *base = (uint8_t *)ptr - 4;
    size_t old_alloc = *(uint32_t *)base;

    if (new_size <= old_alloc) return ptr;  /* no growth needed */

    void *new = heap_backend_malloc(new_size);
    if (!new) return NULL;
    memcpy(new, ptr, old_alloc);
    heap_backend_free(ptr);
    return new;
}

/*
 * heap_backend_calloc — allocate and zero (required by abi_wrappers.c).
 */
void *heap_backend_calloc(size_t n, size_t s)
{
    size_t total = n * s;
    if (s != 0 && total / s != n) return NULL;  /* overflow check */
    void *p = heap_backend_malloc(total);
    if (p) memset(p, 0, total);
    return p;
}

/*
 * heap_backend_usable_size — return the actual allocated size.
 */
size_t heap_backend_usable_size(void *ptr)
{
    if (!ptr) return 0;
    uint8_t *base = (uint8_t *)ptr - 4;
    return *(uint32_t *)base;
}

void *heap_backend_aligned_alloc(size_t align, size_t len)
{
    (void)align;
    return heap_backend_malloc(len);
}

#endif /* __i386__ */
