#ifndef MY_WINE_SYSCALL_SAFE_UTILS_H
#define MY_WINE_SYSCALL_SAFE_UTILS_H

#include <stdint.h>
#include <stddef.h>
#include "src/syscall/syscalls_inline.h"

#ifndef AT_FDCWD
#define AT_FDCWD ((long)-100)
#endif
#ifndef O_RDONLY
#define O_RDONLY 0
#endif

extern int g_debug_level;

#if defined(__GNUC__)
#define SYSCALL_SAFE_INLINE static inline __attribute__((always_inline))
#else
#define SYSCALL_SAFE_INLINE static inline
#endif

SYSCALL_SAFE_INLINE unsigned char syscall_safe_ascii_lower(unsigned char c)
{
    return (c >= 'A' && c <= 'Z') ? (unsigned char)(c + 32) : c;
}

SYSCALL_SAFE_INLINE size_t syscall_safe_strlen(const char *s)
{
    size_t n = 0;
    const volatile char *p = (const volatile char *)s;
    while (p[n] != '\0')
        n++;
    return n;
}

SYSCALL_SAFE_INLINE int syscall_safe_strcmp(const char *a, const char *b)
{
    while (*a != '\0' && *b != '\0') {
        if (*a != *b)
            return (int)(unsigned char)*a - (int)(unsigned char)*b;
        a++;
        b++;
    }
    return (int)(unsigned char)*a - (int)(unsigned char)*b;
}

SYSCALL_SAFE_INLINE int syscall_safe_strcasecmp(const char *a, const char *b)
{
    while (*a != '\0' && *b != '\0') {
        unsigned char ca = syscall_safe_ascii_lower((unsigned char)*a);
        unsigned char cb = syscall_safe_ascii_lower((unsigned char)*b);
        if (ca != cb)
            return (int)ca - (int)cb;
        a++;
        b++;
    }
    return (int)syscall_safe_ascii_lower((unsigned char)*a) -
           (int)syscall_safe_ascii_lower((unsigned char)*b);
}

SYSCALL_SAFE_INLINE int syscall_safe_strncmp(const char *a, const char *b, size_t n)
{
    while (n != 0 && *a != '\0' && *a == *b) {
        a++;
        b++;
        n--;
    }
    if (n == 0)
        return 0;
    return (int)(unsigned char)*a - (int)(unsigned char)*b;
}

SYSCALL_SAFE_INLINE const char *syscall_safe_strchr(const char *s, int c)
{
    unsigned char uc = (unsigned char)c;
    do {
        if ((unsigned char)*s == uc)
            return s;
    } while (*s++ != '\0');
    return NULL;
}

SYSCALL_SAFE_INLINE const char *syscall_safe_strrchr(const char *s, int c)
{
    const char *result = NULL;
    unsigned char uc = (unsigned char)c;
    do {
        if ((unsigned char)*s == uc)
            result = s;
    } while (*s++ != '\0');
    return result;
}

SYSCALL_SAFE_INLINE void *syscall_safe_memcpy(void *dst, const void *src, size_t n)
{
    volatile unsigned char *d = (volatile unsigned char *)dst;
    const volatile unsigned char *s = (const volatile unsigned char *)src;
    for (size_t i = 0; i < n; i++)
        d[i] = s[i];
    return dst;
}

SYSCALL_SAFE_INLINE void syscall_safe_memset(void *ptr, int c, size_t n)
{
    volatile unsigned char *p = (volatile unsigned char *)ptr;
    unsigned char uc = (unsigned char)c;
    for (size_t i = 0; i < n; i++)
        p[i] = uc;
}

SYSCALL_SAFE_INLINE void syscall_safe_copy_str(char *dst, const char *src, size_t max_len)
{
    size_t i;

    if (max_len == 0)
        return;
    volatile char *d = (volatile char *)dst;
    const volatile char *s = (const volatile char *)src;
    for (i = 0; i < max_len - 1 && s[i] != '\0'; i++)
        d[i] = s[i];
    d[i] = '\0';
}

SYSCALL_SAFE_INLINE int syscall_safe_build_path(char *dst, size_t dst_size,
                                                const char *dir, const char *name)
{
    size_t dir_len = syscall_safe_strlen(dir);
    size_t name_len = syscall_safe_strlen(name);

    if (dir_len + 1 + name_len + 1 > dst_size)
        return -1;
    syscall_safe_memcpy(dst, dir, dir_len);
    dst[dir_len] = '/';
    syscall_safe_memcpy(dst + dir_len + 1, name, name_len);
    dst[dir_len + 1 + name_len] = '\0';
    return 0;
}

SYSCALL_SAFE_INLINE int syscall_safe_path_exists(const char *path)
{
    long fd = INLINE_SYSCALL_OPENAT(AT_FDCWD, path, O_RDONLY, 0);
    if (fd < 0)
        return 0;
    INLINE_SYSCALL_CLOSE(fd);
    return 1;
}

SYSCALL_SAFE_INLINE int syscall_safe_add_overflow_size(size_t a, size_t b,
                                                       size_t *out)
{
    if (a > (size_t)-1 - b)
        return 1;
    *out = a + b;
    return 0;
}

SYSCALL_SAFE_INLINE int syscall_safe_range_valid_size(size_t off, size_t len,
                                                      size_t limit)
{
    return off <= limit && len <= limit - off;
}

SYSCALL_SAFE_INLINE void syscall_safe_format_hex(char *dst, uintptr_t val,
                                                 size_t nibbles)
{
    static const char hex[] = "0123456789abcdef";
    for (size_t i = 0; i < nibbles; i++) {
        size_t shift = (nibbles - 1 - i) * 4;
        dst[i] = hex[(val >> shift) & 0xf];
    }
}

SYSCALL_SAFE_INLINE void syscall_safe_stderr_write(const char *msg, size_t len)
{
    INLINE_SYSCALL_WRITE_ERR(msg, len);
}

SYSCALL_SAFE_INLINE void syscall_safe_stderr_write_cstr(const char *msg)
{
    syscall_safe_stderr_write(msg, syscall_safe_strlen(msg));
}

SYSCALL_SAFE_INLINE void syscall_safe_debug_write_str(int level,
                                                      const char *prefix,
                                                      const char *str)
{
    char buf[256];
    size_t i = 0;

    if (g_debug_level < level)
        return;
    for (const char *p = prefix; *p != '\0' && i < sizeof(buf) - 16; p++)
        buf[i++] = *p;
    for (const char *p = str; *p != '\0' && i < sizeof(buf) - 2; p++)
        buf[i++] = *p;
    buf[i++] = '\n';
    INLINE_SYSCALL_WRITE(2, buf, i);
}

SYSCALL_SAFE_INLINE void syscall_safe_debug_write_ptr(int level,
                                                      const char *prefix,
                                                      uintptr_t val)
{
    char buf[96];
    size_t i = 0;
    const size_t nibbles = sizeof(uintptr_t) * 2;

    if (g_debug_level < level)
        return;
    for (const char *p = prefix; *p != '\0' && i < sizeof(buf) - 3 - nibbles; p++)
        buf[i++] = *p;
    buf[i++] = '0';
    buf[i++] = 'x';
    syscall_safe_format_hex(buf + i, val, nibbles);
    i += nibbles;
    buf[i++] = '\n';
    INLINE_SYSCALL_WRITE(2, buf, i);
}

SYSCALL_SAFE_INLINE void syscall_safe_debug_write_bool(int level,
                                                       const char *prefix,
                                                       int val)
{
    char buf[64];
    size_t i = 0;

    if (g_debug_level < level)
        return;
    for (const char *p = prefix; *p != '\0' && i < sizeof(buf) - 3; p++)
        buf[i++] = *p;
    buf[i++] = val ? '1' : '0';
    buf[i++] = '\n';
    INLINE_SYSCALL_WRITE(2, buf, i);
}

SYSCALL_SAFE_INLINE int syscall_safe_guest_ptr_is_valid(uint64_t ptr)
{
    if (ptr == 0)
        return 0;
#if defined(__i386__)
    if (ptr > 0xFFFF8000UL || (ptr & 3) != 0)
        return 0;
#else
    if (ptr > 0xfffffffffffe0000UL || (ptr & 7) != 0)
        return 0;
#endif
    return 1;
}

SYSCALL_SAFE_INLINE void syscall_safe_guest_write_ptr(void *base, size_t offset,
                                                      void *value, int is_32bit)
{
    if (is_32bit) {
        *(uint32_t *)((char *)base + offset) = (uint32_t)(uintptr_t)value;
    } else {
        *(uint64_t *)((char *)base + offset) = (uint64_t)(uintptr_t)value;
    }
}

SYSCALL_SAFE_INLINE void syscall_safe_guest_write_u8(void *base, size_t offset,
                                                     uint8_t value)
{
    *(uint8_t *)((char *)base + offset) = value;
}

#undef SYSCALL_SAFE_INLINE

#endif /* MY_WINE_SYSCALL_SAFE_UTILS_H */
