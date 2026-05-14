/*
 * loader_utils.h — Syscall-safe string/memory helpers for the PE loader
 *
 * Truly inline (no __builtin_*, no glibc PLT) implementations of common
 * string and memory utilities.  Safe to call from WINE_STUB context
 * after the GS→TEB switch, where any glibc call crashes.
 *
 * Implemented as macros using GNU statement expressions ({ ... }) to
 * guarantee zero function-call overhead and zero external symbol
 * references (no PLT/GOT entries).
 *
 * Shared between import_resolve.c, module_list.c, and other loader code.
 */

#ifndef MY_WINE_LOADER_UTILS_H
#define MY_WINE_LOADER_UTILS_H

#include <stdint.h>
#include <stddef.h>
#include <fcntl.h>
#include "../syscall/syscalls_inline.h"

/* ── String helpers (all macros — guaranteed no external calls) ── */

#define dll_strcasecmp(_a, _b)                                         \
    ({                                                                 \
        int _dll_rc = 0;                                               \
        const char *_dll_a = (_a), *_dll_b = (_b);                     \
        while (*_dll_a && *_dll_b) {                                   \
            unsigned char _ca = *_dll_a, _cb = *_dll_b;                \
            if (_ca >= 'A' && _ca <= 'Z') _ca += 32;                   \
            if (_cb >= 'A' && _cb <= 'Z') _cb += 32;                   \
            if (_ca != _cb) { _dll_rc = (int)_ca - (int)_cb; break; }  \
            _dll_a++; _dll_b++;                                        \
        }                                                              \
        if (!_dll_rc) {                                                \
            unsigned char _ca = *_dll_a, _cb = *_dll_b;                \
            if (_ca >= 'A' && _ca <= 'Z') _ca += 32;                   \
            if (_cb >= 'A' && _cb <= 'Z') _cb += 32;                   \
            _dll_rc = (int)_ca - (int)_cb;                             \
        }                                                              \
        _dll_rc;                                                       \
    })

#define dll_strlen(_s)                                                 \
    ({                                                                 \
        size_t _dll_n = 0;                                             \
        const char *_dll_p = (_s);                                     \
        while (*_dll_p++) _dll_n++;                                    \
        _dll_n;                                                        \
    })

#define dll_strncmp(_a, _b, _n)                                        \
    ({                                                                 \
        int _dll_rc = 0;                                               \
        size_t _dll_i = (_n);                                          \
        const char *_dll_a = (_a), *_dll_b = (_b);                     \
        while (_dll_i && *_dll_a && (*_dll_a == *_dll_b)) {            \
            _dll_a++; _dll_b++; _dll_i--;                              \
        }                                                              \
        _dll_rc = (unsigned char)*_dll_a - (unsigned char)*_dll_b;     \
        _dll_rc;                                                       \
    })

#define dll_strchr(_s, _c)                                             \
    ({                                                                 \
        const char *_dll_result = NULL;                                \
        const char *_dll_p = (_s);                                     \
        unsigned char _dll_uc = (unsigned char)(_c);                   \
        do {                                                           \
            if (*_dll_p == _dll_uc) { _dll_result = _dll_p; break; }   \
        } while (*_dll_p++);                                          \
        _dll_result;                                                   \
    })

#define dll_strrchr(_s, _c)                                            \
    ({                                                                 \
        const char *_dll_result = NULL;                                \
        const char *_dll_p = (_s);                                     \
        unsigned char _dll_uc = (unsigned char)(_c);                   \
        do {                                                           \
            if (*_dll_p == _dll_uc) _dll_result = _dll_p;              \
        } while (*_dll_p++);                                          \
        _dll_result;                                                   \
    })

/* strncpy-like: copies up to max_len-1 bytes, always null-terminates */
#define dll_copy_str(_dst, _src, _max_len)                             \
    do {                                                               \
        size_t _dll_m = (_max_len);                                    \
        if (_dll_m > 0) {                                              \
            size_t _dll_i = 0;                                         \
            char *_dll_d = (_dst);                                     \
            const char *_dll_s = (_src);                               \
            for (_dll_i = 0; _dll_i < _dll_m - 1 && _dll_s[_dll_i];    \
                 _dll_i++)                                             \
                _dll_d[_dll_i] = _dll_s[_dll_i];                       \
            _dll_d[_dll_i] = '\0';                                     \
        }                                                              \
    } while (0)

/* ── Memory helpers (all macros) ─────────────────────────────── */

#define dll_memset(_ptr, _c, _n)                                       \
    do {                                                               \
        size_t _dll_n = (_n);                                          \
        unsigned char *_dll_p = (unsigned char *)(_ptr);               \
        unsigned char _dll_c = (unsigned char)(_c);                    \
        while (_dll_n--) *_dll_p++ = _dll_c;                           \
    } while (0)

#define dll_memcpy(_dst, _src, _n)                                     \
    ({                                                                 \
        void *_dll_r = (_dst);                                         \
        size_t _dll_n = (_n);                                          \
        unsigned char *_dll_d = (unsigned char *)(_dst);               \
        const unsigned char *_dll_s = (const unsigned char *)(_src);   \
        while (_dll_n--) *_dll_d++ = *_dll_s++;                        \
        _dll_r;                                                        \
    })

/* ── Path helpers (all macros) ───────────────────────────────── */

#define dll_build_path(_dst, _dst_size, _dir, _name)                   \
    ({                                                                 \
        int _dll_rc = 0;                                               \
        char *_dll_dst = (_dst);                                       \
        size_t _dll_ds = (_dst_size);                                  \
        size_t _dll_dl = dll_strlen((_dir));                           \
        size_t _dll_nl = dll_strlen((_name));                          \
        if (_dll_dl + 1 + _dll_nl + 1 > _dll_ds) {                     \
            _dll_rc = -1;                                              \
        } else {                                                       \
            dll_memcpy(_dll_dst, (_dir), _dll_dl);                     \
            _dll_dst[_dll_dl] = '/';                                   \
            dll_memcpy(_dll_dst + _dll_dl + 1, (_name), _dll_nl);      \
            _dll_dst[_dll_dl + 1 + _dll_nl] = '\0';                    \
        }                                                              \
        _dll_rc;                                                       \
    })

#define dll_path_exists(_p)                                            \
    ({                                                                 \
        int _dll_rc = 0;                                               \
        long _dll_fd = INLINE_SYSCALL_OPENAT(AT_FDCWD, (_p), O_RDONLY, 0); \
        if (_dll_fd >= 0) {                                            \
            INLINE_SYSCALL_CLOSE(_dll_fd);                             \
            _dll_rc = 1;                                               \
        }                                                              \
        _dll_rc;                                                       \
    })

#endif /* MY_WINE_LOADER_UTILS_H */
