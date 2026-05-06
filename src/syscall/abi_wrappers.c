#define _GNU_SOURCE

#include <stddef.h>
#include <stdint.h>
#include <unistd.h>
#include <sys/mman.h>
#include "syscalls_inline.h"

#ifndef PAGE_SIZE
#define PAGE_SIZE 4096
#endif

/*
 * abi_wrappers.c — ABI-safe syscall wrappers
 *
 * These functions are called from __attribute__((sysv_abi)) contexts
 * after GS has been switched to TEB. They MUST NOT use glibc, because
 * glibc accesses vDSO via GS-relative offsets and will crash.
 *
 * All memory management uses mmap/munmap (page-sized allocations).
 * All string ops use compiler builtins (no vDSO).
 */

/* malloc header: stored just before the returned pointer */
typedef struct {
    size_t mmap_size;  /* total mmap'd size (page-aligned) */
} malloc_hdr_t;

/* Round up to page size */
static inline size_t page_align(size_t s)
{
    return (s + PAGE_SIZE - 1) & ~(size_t)(PAGE_SIZE - 1);
}

__attribute__((sysv_abi))
void *sysv_malloc(size_t s)
{
    if (s == 0) s = 1;
    /* We need space for the header + the requested data, both page-aligned overall */
    size_t total = page_align(sizeof(malloc_hdr_t) + s);
    void *p = INLINE_SYSCALL_MMAP(NULL, total,
        PROT_READ | PROT_WRITE,
        MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (p == NULL || p == MAP_FAILED) return NULL;
    malloc_hdr_t *hdr = (malloc_hdr_t *)p;
    hdr->mmap_size = total;
    return (void *)(hdr + 1);
}

__attribute__((sysv_abi))
void *sysv_calloc(size_t n, size_t s)
{
    size_t total_req = n * s;
    if (total_req == 0) total_req = 1;
    size_t total = page_align(sizeof(malloc_hdr_t) + total_req);
    void *p = INLINE_SYSCALL_MMAP(NULL, total,
        PROT_READ | PROT_WRITE,
        MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (p == NULL || p == MAP_FAILED) return NULL;
    malloc_hdr_t *hdr = (malloc_hdr_t *)p;
    hdr->mmap_size = total;
    void *user_ptr = (void *)(hdr + 1);
    __builtin_memset(user_ptr, 0, total_req);
    return user_ptr;
}

__attribute__((sysv_abi))
void sysv_free(void *p)
{
    if (p == NULL) return;
    malloc_hdr_t *hdr = (malloc_hdr_t *)((uintptr_t)p - sizeof(malloc_hdr_t));
    (void)INLINE_SYSCALL_MUNMAP(hdr, hdr->mmap_size);
}

__attribute__((sysv_abi))
void *sysv_memcpy(void *d, const void *s, size_t n)
{
    return __builtin_memcpy(d, s, n);
}

__attribute__((sysv_abi))
size_t sysv_strlen(const char *s)
{
    return __builtin_strlen(s);
}

__attribute__((sysv_abi))
int sysv_strncmp(const char *a, const char *b, size_t n)
{
    return __builtin_strncmp(a, b, n);
}

__attribute__((sysv_abi))
void *sysv_mmap(void *a, size_t l, int p, int f, int d, off_t o)
{
    return INLINE_SYSCALL_MMAP(a, l, p, f, d, o);
}

__attribute__((sysv_abi))
int sysv_mprotect(void *a, size_t l, int p)
{
    return (int)INLINE_SYSCALL_MPROTECT(a, l, p);
}
