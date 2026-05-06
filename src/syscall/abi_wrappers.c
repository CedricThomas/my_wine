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
 * Memory management uses musl oldmalloc (arena-based, mmap-backed).
 * String ops use compiler builtins (no vDSO).
 */

/* musl backend (defined in src/heap/musl_malloc_wrapper.c) */
extern void *musl_malloc(size_t);
extern void  musl_free(void *);
extern void *musl_calloc(size_t, size_t);
extern void *musl_realloc(void *, size_t);

__attribute__((sysv_abi))
void *sysv_malloc(size_t s)
{
    return musl_malloc(s);
}

__attribute__((sysv_abi))
void *sysv_calloc(size_t n, size_t s)
{
    return musl_calloc(n, s);
}

__attribute__((sysv_abi))
void sysv_free(void *p)
{
    musl_free(p);
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
