/*
 * loader_utils.h — Syscall-safe string/memory helpers for the PE loader
 *
 * Static inline implementations of common string and memory utilities.
 * Safe to call from WINE_STUB context (no glibc/vDSO access).
 *
 * Shared between import_resolve.c and module_list.c to avoid duplication.
 */

#ifndef MY_WINE_LOADER_UTILS_H
#define MY_WINE_LOADER_UTILS_H

#include <stdint.h>
#include <stddef.h>
#include <fcntl.h>
#include "../syscall/syscalls_inline.h"

/* ── String helpers ──────────────────────────────────────────── */

static inline int dll_strcasecmp(const char *a, const char *b)
{
    while (*a && *b) {
        unsigned char ca = *a, cb = *b;
        if (ca >= 'A' && ca <= 'Z') ca += 32;
        if (cb >= 'A' && cb <= 'Z') cb += 32;
        if (ca != cb) return (int)ca - (int)cb;
        a++; b++;
    }
    unsigned char ca = *a, cb = *b;
    if (ca >= 'A' && ca <= 'Z') ca += 32;
    if (cb >= 'A' && cb <= 'Z') cb += 32;
    return (int)ca - (int)cb;
}

/* strncpy-like: copies up to max_len-1 bytes, always null-terminates */
static inline void dll_copy_str(char *dst, const char *src, size_t max_len)
{
    if (max_len == 0) return; /* avoid SIZE_MAX underflow */
    size_t i;
    for (i = 0; i < max_len - 1 && src[i] != '\0'; i++)
        dst[i] = src[i];
    dst[i] = '\0';
}

/* ── Memory helpers ──────────────────────────────────────────── */

static inline void dll_memset(void *ptr, int c, size_t n)
{
    __builtin_memset(ptr, c, n);
}

/* ── import_resolve.c-only helpers ───────────────────────────── */

static inline size_t dll_strlen(const char *s)
{
    return __builtin_strlen(s);
}

static inline int dll_strncmp(const char *a, const char *b, size_t n)
{
    return __builtin_strncmp(a, b, n);
}

static inline const char *dll_strchr(const char *s, int c)
{
    return __builtin_strchr(s, c);
}

static inline int dll_build_path(char *dst, size_t dst_size,
                                  const char *dir, const char *name)
{
    size_t d_len = dll_strlen(dir);
    size_t n_len = dll_strlen(name);
    if (d_len + 1 + n_len + 1 > dst_size)
        return -1;
    __builtin_memcpy(dst, dir, d_len);
    dst[d_len] = '/';
    __builtin_memcpy(dst + d_len + 1, name, n_len);
    dst[d_len + 1 + n_len] = '\0';
    return 0;
}

static inline int dll_path_exists(const char *p)
{
    long fd = INLINE_SYSCALL_OPENAT(AT_FDCWD, p, O_RDONLY);
    if (fd >= 0) {
        INLINE_SYSCALL_CLOSE(fd);
        return 1;
    }
    return 0;
}

#endif /* MY_WINE_LOADER_UTILS_H */
