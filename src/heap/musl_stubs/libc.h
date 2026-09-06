/*
 * musl_stubs/libc.h — Stub for musl's internal libc.h
 *
 * Provides the minimal definitions needed by the vendored musl malloc files:
 *   libc struct, PAGE_SIZE, hidden, weak_alias
 */

#ifndef MUSL_STUB_LIBC_H
#define MUSL_STUB_LIBC_H

#include <stdlib.h>
#include <limits.h>
#include <stdint.h>

#define hidden
#define weak_alias(x,y)

#define PAGE_SIZE 4096

struct __libc {
    volatile signed char need_locks;
    size_t *auxv;
    size_t page_size;
};

static struct __libc libc = {
    .need_locks = 0,
    .auxv = 0,
    .page_size = PAGE_SIZE
};

#endif
