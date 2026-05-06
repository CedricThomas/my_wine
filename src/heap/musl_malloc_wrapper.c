/*
 * musl_malloc_wrapper.c — musl oldmalloc integration for my_wine
 *
 * Wraps musl's oldmalloc (src/malloc/oldmalloc/) as our heap backend.
 * Replaces the raw mmap-per-allocation approach with a proper arena-based
 * allocator featuring bin management, coalescing.
 *
 * Musl source is compiled inline (via #include) with syscall functions
 * mapped to our INLINE_SYSCALL_* macros. All musl internal headers are
 * replaced with stubs in musl_stubs/.
 *
 * Musl copyright: © 2005-2020 Rich Felker, et al. — MIT License
 */

/* Must be first to expose MREMAP_MAYMOVE from <sys/mman.h> */
#define _GNU_SOURCE

#include <sys/types.h>
#include <sys/mman.h>
#include <errno.h>
#include <string.h>
#include <limits.h>
#include <stdint.h>

/* ── Step 1: Include musl stubs (defines libc, atomics, etc.) ── */

#include "musl_stubs/libc.h"        /* libc struct, PAGE_SIZE, hidden, weak_alias */
#include "musl_stubs/atomic.h"      /* x86_64 atomic primitives + generic fallbacks */
#include "musl_stubs/pthread_impl.h" /* __wait, __wake, __timedwait (no-ops) */
#include "musl_stubs/dynlink.h"     /* __malloc_replaced, __aligned_alloc_replaced */
#include "musl_stubs/malloc_impl.h" /* struct chunk, struct bin, macros */
#include "musl_stubs/fork_impl.h"   /* empty — malloc.c defines __malloc_atfork itself */

/* ── Step 2: Define externals from dynlink.h stub ── */

int __malloc_replaced = 0;
int __aligned_alloc_replaced = 0;

/* ── Step 3: Include our syscall macros ── */

#include "../syscall/syscalls_inline.h"

/* ── Step 4: Define syscall function wrappers that musl expects ── */

/* SYS_brk for musl's __syscall(SYS_brk, ...) calls.
 * __NR_brk is defined in <asm/unistd_64.h> via syscalls_inline.h */
#define SYS_brk __NR_brk

/* __mmap — maps directly to our inline syscall */
static void *__mmap(void *addr, size_t len, int prot, int flags, int fd, off_t offset)
{
    return INLINE_SYSCALL_MMAP(addr, len, prot, flags, fd, offset);
}

/* __munmap — maps directly to our inline syscall */
static int __munmap(void *addr, size_t len)
{
    return (int)INLINE_SYSCALL_MUNMAP(addr, len);
}

/* __madvise — no-op (MADV_DONTNEED is an optional optimization) */
static int __madvise(void *addr, size_t len, int advice)
{
    (void)addr; (void)len; (void)advice;
    return 0;
}

/* __mremap — fallback: alloc new + copy + free old.
 * musl's realloc() uses this for mmapped chunks. */
static void *__mremap(void *old_addr, size_t old_size, size_t new_size, unsigned long flags)
{
    (void)flags; /* We don't use MREMAP_MAYMOVE; always alloc fresh */
    void *new = __mmap(0, new_size, PROT_READ | PROT_WRITE,
                       MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (new == (void *)-1) return (void *)-1;
    size_t copy_len = old_size < new_size ? old_size : new_size;
    memcpy(new, old_addr, copy_len);
    __munmap(old_addr, old_size);
    return new;
}

/* __syscall — always returns -1 (failure) so musl falls through to mmap.
 * musl calls: brk = __syscall(SYS_brk, 0); and __syscall(SYS_brk, brk+n)
 * Returning 0 or negative from the first call means brk path fails. */
static long __syscall(long n, ...)
{
    (void)n;
    return -1;
}

/* ── Step 5: Forward declaration for __libc_free ──
 * musl's malloc.c does: #define free __libc_free
 * Then realloc() calls free() which becomes __libc_free().
 * But the definition of __libc_free (as "void free()") comes later.
 * We need a forward declaration so realloc's call to __libc_free is valid. */

extern void __libc_free(void *);

/* ── Step 6: Include musl source files ──
 * By this point all required symbols (libc, atomics, __mmap, etc.)
 * are defined. The #include "..." directives inside musl source
 * resolve to our stubs via -Isrc/heap/musl_stubs. */

#include "musl_src/malloc.c"
#include "musl_src/aligned_alloc.c"
#include "musl_src/malloc_usable_size.c"

/* ── Step 7: Export wrapper functions for use by abi_wrappers.c ──
 * These are the public interface. The musl internal functions
 * (__libc_malloc_impl, __libc_realloc, __libc_free) are used here. */

__attribute__((sysv_abi))
void *musl_malloc(size_t s)
{
    return __libc_malloc_impl(s);
}

__attribute__((sysv_abi))
void *musl_calloc(size_t n, size_t s)
{
    size_t total = n * s;
    if (n && s > (size_t)-1 / n) return 0;
    if (total == 0) total = 1;
    void *p = __libc_malloc_impl(total);
    if (p) memset(p, 0, total);
    return p;
}

__attribute__((sysv_abi))
void *musl_realloc(void *p, size_t s)
{
    return __libc_realloc(p, s);
}

__attribute__((sysv_abi))
void musl_free(void *p)
{
    __libc_free(p);
}

__attribute__((sysv_abi))
void *musl_aligned_alloc(size_t align, size_t len)
{
    return aligned_alloc(align, len);
}

__attribute__((sysv_abi))
size_t musl_malloc_usable_size(void *p)
{
    return malloc_usable_size(p);
}
