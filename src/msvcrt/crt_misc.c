/*
 * crt_misc.c — Additional MSVCRT stubs for CRT locale, errno, locking,
 *              and character conversion functions.
 *
 * These functions are called from within the PE after GS has been
 * switched to TEB. They MUST NOT use glibc (vDSO via GS crash).
 * All implementations use either direct syscalls or pure-C logic.
 */

#define _GNU_SOURCE

#include <locale.h>
#include <wchar.h>
#include <errno.h>
#include <stdint.h>
#include <string.h>
#include "msvcrt_priv.h"
#include "../syscall/syscalls_inline.h"

/* ── ___lc_codepage_func ───────────────────────────────────── */
/*
 * Internal MSVCRT function that returns the current code page.
 * Return 65001 (UTF-8) as a reasonable default.
 */
WINE_STUB
int ___lc_codepage_func(void)
{
    return 65001;
}

/* ── ___mb_cur_max_func ────────────────────────────────────── */
/*
 * Internal MSVCRT function that returns the current max bytes per
 * multibyte character. For UTF-8, this is 4 (but mingw often expects 1
 * for ASCII locale). Return 6 to cover UTF-8 maximum.
 */
WINE_STUB
int ___mb_cur_max_func(void)
{
    return 6;
}

int __mb_cur_max = 6;

/* ── _errno ────────────────────────────────────────────────── */
/*
 * Thread-local errno storage that the MSVCRT expects at a known address.
 * We provide our own static copy so the PE can write to it.
 */
static int g_msvcrt_errno = 0;

/* Wrapper function for when imported as a function pointer */
WINE_STUB
int *_errno_func(void)
{
    return FORCE_PTR_RETURN(&g_msvcrt_errno);
}

/* ── _lock / _unlock ───────────────────────────────────────── */
/*
 * MSVCRT internal locking functions used by some CRT operations.
 * Stub as no-ops — we don't need real locking for these paths.
 */
WINE_STUB
void _lock(int locknum)
{
    (void)locknum;
}

WINE_STUB
void _unlock(int locknum)
{
    (void)locknum;
}

/* ── fputc ─────────────────────────────────────────────────── */
/*
 * Write a single character to a file. The stream is validated against
 * our __wine_iob array, then written via direct syscall.
 */
static int wine_fputc(int ch, void *stream)
{
    if (stream == NULL) return -1;
    uintptr_t base = (uintptr_t)g_crt.iob.bytes;
    uintptr_t addr = (uintptr_t)stream;
    if (addr < base || addr >= base + WINE_FILE_SIZE * 3) return -1;
    wine_FILE *f = (wine_FILE *)stream;
    int fd = f->_fd;
    if (fd < 0 || fd > 2) return -1;

    char c = (char)ch;
    (void)INLINE_SYSCALL_WRITE(fd, &c, 1);
    return ch;
}

/* ── localeconv ────────────────────────────────────────────── */
/*
 * Return a pointer to a static lconv structure with US/English defaults.
 * Initialized once and reused.
 */
static struct lconv g_msvcrt_lconv;
static int g_lconv_init = 0;

static struct lconv *wine_localeconv(void)
{
    if (!g_lconv_init) {
        __builtin_memset(&g_msvcrt_lconv, 0, sizeof(g_msvcrt_lconv));
        g_msvcrt_lconv.decimal_point = ".";
        g_msvcrt_lconv.thousands_sep = "";
        g_msvcrt_lconv.currency_symbol = "";
        g_msvcrt_lconv.int_curr_symbol = "USD";
        g_msvcrt_lconv.frac_digits = 2;
        g_msvcrt_lconv.p_cs_precedes = 0;
        g_msvcrt_lconv.n_cs_precedes = 0;
        g_lconv_init = 1;
    }
    return &g_msvcrt_lconv;
}

/* ── strerror ──────────────────────────────────────────────── */
/*
 * Return error string for error number. Use a small static table
 * to avoid calling glibc strerror which may access vDSO via GS.
 */
static const char *wine_strerror(int errnum)
{
    static const char *msgs[] = {
        [0] = "Success",
        [1] = "Operation not permitted",
        [2] = "No such file or directory",
        [3] = "No such process",
        [4] = "Interrupted system call",
        [5] = "Input/output error",
        [6] = "No such device or address",
        [7] = "Argument list too long",
        [8] = "Exec format error",
        [9] = "Bad file number",
        [10] = "No child processes",
        [11] = "Resource temporarily unavailable",
        [12] = "Cannot allocate memory",
        [13] = "Permission denied",
        [14] = "Bad address",
        [22] = "Invalid argument",
        [26] = "Text file busy",
        [79] = "Bad file descriptor",
        [87] = "Invalid parameter",
        [122] = "Buffer too small",
        [139] = "Segmentation fault",
    };

    if (errnum >= 0 && errnum <= 139) {
        if (msgs[errnum] != NULL) return msgs[errnum];
    }
    return "Unknown error";
}

WINE_STUB
int _m_atoi(const char *s)
{
    int sign = 1;
    int value = 0;

    if (!s)
        return 0;
    while (*s == ' ' || *s == '\t' || *s == '\n' || *s == '\r')
        s++;
    if (*s == '-') {
        sign = -1;
        s++;
    } else if (*s == '+') {
        s++;
    }
    while (*s >= '0' && *s <= '9') {
        value = value * 10 + (*s - '0');
        s++;
    }
    return value * sign;
}

WINE_STUB
char *_m_strchr(const char *s, int c)
{
    unsigned char needle = (unsigned char)c;

    if (!s)
        return NULL;
    while (*s) {
        if ((unsigned char)*s == needle)
            return (char *)s;
        s++;
    }
    if (needle == 0)
        return (char *)s;
    return NULL;
}

WINE_STUB
char *_m_setlocale(int category, const char *locale)
{
    static char c_locale[] = "C";
    (void)category;
    (void)locale;
    return c_locale;
}

/* ── wcslen ────────────────────────────────────────────────── */
/*
 * Wide-character string length. Pure C, no libc dependency.
 */
static size_t wine_wcslen(const wchar_t *s)
{
    size_t len = 0;
    while (s[len] != 0) len++;
    return len;
}

/* ── Export function pointers for import table ─────────────── */

void *__msvcrt___lc_codepage_func = (void *)___lc_codepage_func;
void *__msvcrt___mb_cur_max_func  = (void *)___mb_cur_max_func;
void *__msvcrt__errno_func        = (void *)_errno_func;
void *__msvcrt__lock              = (void *)_lock;
void *__msvcrt__unlock            = (void *)_unlock;
void *__msvcrt_fputc              = (void *)wine_fputc;
void *__msvcrt_localeconv         = (void *)wine_localeconv;
void *__msvcrt_strerror           = (void *)wine_strerror;
void *__msvcrt_wcslen             = (void *)wine_wcslen;
