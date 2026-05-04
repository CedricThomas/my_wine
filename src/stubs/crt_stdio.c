/*
 * crt_stdio.c — Stdio stubs for MSVCRT.
 *
 * wine_vfprintf, wine_fprintf, wine_fwrite and their __msvcrt_* exports.
 * Uses direct syscalls to avoid callee-save SSE spills from the PE.
 */

#define _GNU_SOURCE
#define CRT_STDIO_C

#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <stdarg.h>
#include "msvcrt_priv.h"
#include "../syscalls_inline.h"

/* ── Internal implementations ──────────────────────────────── */

WINE_STUB_STATIC
int wine_vfprintf(wine_FILE *stream, const char *format, va_list ap)
{
    if (stream == NULL) return -1;
    uintptr_t base = (uintptr_t)__wine_iob.bytes;
    uintptr_t addr = (uintptr_t)stream;
    if (addr < base || addr >= base + WINE_FILE_SIZE * 3) return -1;
    int fd = stream->_fd;
    if (fd < 0 || fd > 2) return -1;
    (void)ap;  /* avoid compiler warning; we don't dereference garbage va_list */

    /* Instead of calling vsnprintf (which crashes on garbage va_list from PE),
     * write the format string directly. This handles most CRT startup output. */
    size_t len = 0;
    while (len < 4095 && format[len]) len++;
    if (len == 0) return 0;

    long res = INLINE_SYSCALL_WRITE(fd, format, len);
    (void)res;
    return (int)len;
}

WINE_STUB_STATIC
int wine_fprintf(wine_FILE *stream, const char *format, ...)
{
    va_list ap;
    va_start(ap, format);
    int ret = wine_vfprintf(stream, format, ap);
    va_end(ap);
    return ret;
}

WINE_STUB_STATIC
size_t wine_fwrite(const void *ptr, size_t size, size_t nmemb, wine_FILE *stream)
{
    if (stream == NULL) return 0;
    uintptr_t base = (uintptr_t)__wine_iob.bytes;
    uintptr_t addr = (uintptr_t)stream;
    if (addr < base || addr >= base + WINE_FILE_SIZE * 3) return 0;
    int fd = stream->_fd;
    if (fd < 0 || fd > 2) return 0;

    size_t total = size * nmemb;
    /* Use syscall directly to avoid callee-save SSE spills */
    long res = INLINE_SYSCALL_WRITE(fd, ptr, total);
    if (res < 0) return 0;
    return (size_t)res / size;
}

/* ── Expose function pointers for import table ─────────────── */

void *__msvcrt_fprintf    = (void *)wine_fprintf;
void *__msvcrt_fwrite     = (void *)wine_fwrite;
void *__msvcrt_vfprintf   = (void *)wine_vfprintf;
