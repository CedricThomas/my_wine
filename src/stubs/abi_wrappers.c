#define _GNU_SOURCE

#include <stddef.h>
#include <stdint.h>
#include <unistd.h>
#include <sys/mman.h>
#include "../syscalls_inline.h"

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

/* Round up to page size */
static inline size_t page_align(size_t s)
{
    return (s + PAGE_SIZE - 1) & ~(size_t)(PAGE_SIZE - 1);
}

__attribute__((sysv_abi))
void *sysv_malloc(size_t s)
{
    if (s == 0) s = 1;
    return INLINE_SYSCALL_MMAP(NULL, page_align(s),
        PROT_READ | PROT_WRITE,
        MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
}

__attribute__((sysv_abi))
void *sysv_calloc(size_t n, size_t s)
{
    size_t total = n * s;
    if (total == 0) total = 1;
    void *p = INLINE_SYSCALL_MMAP(NULL, page_align(total),
        PROT_READ | PROT_WRITE,
        MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (p != NULL && p != MAP_FAILED)
        __builtin_memset(p, 0, total);
    return p;
}

__attribute__((sysv_abi))
void sysv_free(void *p)
{
    if (p == NULL) return;
    /* We allocated page-sized chunks via mmap. munmap the whole page.
     * Note: this is a best-effort deallocation — it works for the
     * allocations we make (page-aligned mmap). For glibc-allocated
     * memory, this is undefined, but we no longer use glibc post-GS. */
    uintptr_t base = (uintptr_t)p & ~(uintptr_t)(PAGE_SIZE - 1);
    (void)INLINE_SYSCALL_MUNMAP((void *)base, PAGE_SIZE);
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
