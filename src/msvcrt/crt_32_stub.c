/*
 * crt_32_stub.c — Minimal CRT startup stubs for the 32-bit and 64-bit builds.
 *
 * Provides the functions needed by MinGW CRT startup that are excluded
 * from the build by the Makefile's crt_*.c filter.
 *
 * These are deliberately minimal — they avoid all libc/TLS dependencies
 * and use direct syscalls where needed (32-bit) or libc fallbacks (64-bit).
 *
 * Functions prefixed with _m_ avoid conflicts with system header declarations.
 */

#include <stdint.h>
#include <stddef.h>

#ifdef MY_WINE32
#include "../syscall/syscalls_inline.h"
#include "../include/wine_abi.h"
#else
#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>
#endif
#include "include/common.h"

/* ── CRT globals (minimal) ─────────────────────────────────── */

int _fmode      = 0;       /* text mode (MinGW default) */
int _commode    = 0;       /* text mode */
int __msvcrt_app_type = 0; /* console app */

/* _acmdln — command line (filled by _getcmdline or left empty) */
char _acmdln[256] = "";

/* _initenv — environment pointer */
extern char **environ;
char **_my_wine_initenv = NULL;
char **__initenv = NULL;

/* Pointer variables expected by MinGW CRT */
char *__p__acmdln = _acmdln;
char *__p__commode = (char*)&_commode;
char *__p__fmode = (char*)&_fmode;

/* Wrapper functions that return pointer values.
 * These MUST be functions (not data) because MinGW CRT imports them
 * via JMP thunks — if the IAT contains a data address, the CPU will
 * try to execute it as instructions → SIGSEGV.
 */
char **__p__acmdln_func(void) { return &__p__acmdln; }
char *__p__fmode_func(void) { return (char*)&_fmode; }
char *__p__commode_func(void) { return (char*)&_commode; }
char ***__initenv_func(void) { return &__initenv; }

/* ── CRT startup functions ─────────────────────────────────── */

/* __getmainargs — MinGW CRT wants argc, argv, envp set up.
 *
 * Build a minimal argv from _acmdln as a safety net if the CRT path
 * is taken (e.g. when _mainCRTStartup calls us). argv points into the
 * static _acmdln buffer so it outlives the call stack. */
static char *g_argv_fallback[2];

WINE_STUB
void __getmainargs(int *argc, char ***argv, char ***envp, int expand_wildcards, void *pStartInfo)
{
    (void)expand_wildcards;
    (void)pStartInfo;
    if (argc) *argc = 1;
    if (argv) {
        /* Build minimal argv = { _acmdln, NULL } as fallback */
        g_argv_fallback[0] = _acmdln[0] ? _acmdln : (char *)"";
        g_argv_fallback[1] = NULL;
        *argv = g_argv_fallback;
    }
    if (envp) *envp = _my_wine_initenv ? _my_wine_initenv : environ;
}

/* __initenv — MinGW CRT expects a function returning env pointer */

/* _initterm — CRT initialization. Empty in our stub. */
WINE_STUB
void _initterm(void)
{
    /* No-op — we don't need CRT initialization in my_wine */
}

/* _initterm_e — CRT init with init/exit function pointers */
WINE_STUB
void *_initterm_e(const void **pi, const void **pe)
{
    if (pi) *pi = NULL;
    if (pe) *pe = NULL;
    return NULL;
}

/* _onexit — no-op */
WINE_STUB
void *_onexit(void (*func)(void))
{
    (void)func;
    return NULL;
}

/* __setusermatherr — no-op */
WINE_STUB
void __setusermatherr(void (*handler)(void))
{
    (void)handler;
}

/* __set_app_type — set console/gui (no-op) */
WINE_STUB
void __set_app_type(int type)
{
    __msvcrt_app_type = type;
}

/* _amsg_exit — error exit */
#ifdef MY_WINE32
WINE_STUB
void _amsg_exit(int msg)
{
    (void)msg;
    INLINE_SYSCALL_EXIT(1);
}
#else
WINE_STUB
void _amsg_exit(int msg)
{
    (void)msg;
    exit(1);
}
#endif

/* _cexit — clean exit */
#ifdef MY_WINE32
WINE_STUB
void _cexit(void)
{
    INLINE_SYSCALL_EXIT(0);
}
#else
WINE_STUB
void _cexit(void)
{
    exit(0);
}
#endif

/* _errno — return errno pointer (use a local variable) */
static int __my_wine_errno;
WINE_STUB
int *_errno(void)
{
    return &__my_wine_errno;
}

/* _lock / _unlock — no-ops (single-threaded) */
WINE_STUB
void _lock(int type) { (void)type; }
WINE_STUB
void _unlock(int type) { (void)type; }

/* __iob_func — not needed in 32-bit (no stdio) */
WINE_STUB
void *__iob_func(void) { return NULL; }

/* __acrt_iob_func — not needed in 32-bit */
WINE_STUB
void *__acrt_iob_func(void) { return NULL; }

/* __lconv_init — locale (no-op) */
WINE_STUB
void __lconv_init(void) { }

/* ___lc_codepage_func — codepage (UTF-8) */
WINE_STUB
int ___lc_codepage_func(void) { return 65001; }

/* ___mb_cur_max_func — max bytes per char */
WINE_STUB
int ___mb_cur_max_func(void) { return 1; }

/* ── Stdlib stubs (needed by MinGW CRT) ─────────────────────── */

/* Heap functions from wine_heap.c */
extern void *HeapCreate(unsigned int fl, size_t dwInitialSize, size_t dwMaximumSize);
extern void *HeapAlloc(void *hHeap, unsigned int dwFlags, size_t dwBytes);
extern int HeapFree(void *hHeap, unsigned int dwFlags, void *lpMem);
extern void *HeapReAlloc(void *hHeap, unsigned int dwFlags, void *lpMem, size_t dwBytes);
extern void *GetProcessHeap(void);

static void *g_process_heap = NULL;

WINE_STUB
void *_m_malloc(size_t size)
{
    if (!g_process_heap) g_process_heap = HeapCreate(0, 65536, 0);
    return HeapAlloc(g_process_heap, 0, size);
}
WINE_STUB
void _m_free(void *ptr)
{
    if (ptr && g_process_heap) HeapFree(g_process_heap, 0, ptr);
}
WINE_STUB
void *_m_calloc(size_t nmemb, size_t size)
{
    void *p = _m_malloc(nmemb * size);
    if (p) __builtin_memset(p, 0, nmemb * size);
    return p;
}
WINE_STUB
void *_m_realloc(void *ptr, size_t size)
{
    if (!ptr) return _m_malloc(size);
    return HeapReAlloc(g_process_heap, 0, ptr, size);
}

/* memcpy / memset */
WINE_STUB
void *_m_memcpy(void *dst, const void *src, size_t n) { return __builtin_memcpy(dst, src, n); }
WINE_STUB
void *_m_memset(void *dst, int c, size_t n) { return __builtin_memset(dst, c, n); }

/* strlen / strnlen / strcmp / strncmp */
WINE_STUB
size_t _m_strlen(const char *s) { return __builtin_strlen(s); }

/* _m_strnlen — bounded string length (used by pe32_entry.c my_getenv) */
WINE_STUB
size_t _m_strnlen(const char *s, size_t n)
{
    size_t i;
    for (i = 0; i < n && s[i]; i++)
        ;
    return i;
}

WINE_STUB
int _m_strcmp(const char *a, const char *b) {
    while (*a && *b) { if (*a != *b) return (unsigned char)*a - (unsigned char)*b; a++; b++; }
    return (unsigned char)*a - (unsigned char)*b;
}
WINE_STUB
int _m_strncmp(const char *a, const char *b, size_t n) {
    while (n > 0 && *a && *b) { if (*a != *b) return (unsigned char)*a - (unsigned char)*b; a++; b++; n--; }
    if (n == 0) return 0;  /* all n characters matched */
    return (unsigned char)*a - (unsigned char)*b;
}

/* Case-insensitive comparison helpers — hand-rolled to avoid musl ifuncs in 32-bit build.
 * These are used by find_section_by_name in pe_headers.c which is compiled for both
 * 32-bit and 64-bit. The 32-bit build links crt_32_stub.c which provides these symbols;
 * the 64-bit build uses the system strncasecmp/strcasecmp instead. */
static inline int _m_tolower_c(unsigned char c) {
    return (c >= 'A' && c <= 'Z') ? (c + 32) : c;
}

int _m_strcasecmp(const char *a, const char *b) {
    while (*a && *b) {
        int ca = _m_tolower_c((unsigned char)*a);
        int cb = _m_tolower_c((unsigned char)*b);
        if (ca != cb) return ca - cb;
        a++; b++;
    }
    return _m_tolower_c((unsigned char)*a) - _m_tolower_c((unsigned char)*b);
}

int _m_strncasecmp(const char *a, const char *b, size_t n) {
    while (n > 0 && *a && *b) {
        int ca = _m_tolower_c((unsigned char)*a);
        int cb = _m_tolower_c((unsigned char)*b);
        if (ca != cb) return ca - cb;
        a++; b++; n--;
    }
    if (n == 0) return 0;
    return _m_tolower_c((unsigned char)*a) - _m_tolower_c((unsigned char)*b);
}

WINE_STUB
int _m_memcmp(const void *a, const void *b, size_t n) { return __builtin_memcmp(a, b, n); }

/* abort / exit */
#ifdef MY_WINE32
WINE_STUB
void _m_abort(void) { INLINE_SYSCALL_EXIT(1); }
WINE_STUB
void _m_exit(int code) { INLINE_SYSCALL_EXIT(code); }
#else
WINE_STUB
void _m_abort(void) { abort(); }
WINE_STUB
void _m_exit(int code) { exit(code); }
#endif

/* signal — returns old handler (simplified) */
typedef void (*sig_handler_t)(int);
WINE_STUB
sig_handler_t _m_signal(int sig, sig_handler_t handler) { (void)sig; (void)handler; return NULL; }

/* wcslen */
WINE_STUB
size_t _m_wcslen(const void *s) { (void)s; return 0; }

/* localeconv / strerror */
struct lconv;
WINE_STUB
struct lconv *_m_localeconv(void) { return NULL; }
WINE_STUB
char *_m_strerror(int n) { (void)n; return "error"; }

/* fprintf / fwrite / vfprintf / fputc */
#ifdef MY_WINE32
/* 32-bit: minimal stubs (no stdio in the build) */
WINE_STUB
int _m_fprintf(void *stream, const char *fmt, ...) { (void)stream; (void)fmt; return 0; }
WINE_STUB
int _m_fwrite(const void *ptr, size_t size, size_t nmemb, void *stream) { (void)ptr; (void)size; (void)nmemb; (void)stream; return 0; }
WINE_STUB
int _m_vfprintf(void *stream, const char *fmt, void *ap) { (void)stream; (void)fmt; (void)ap; return 0; }
WINE_STUB
int _m_fputc(int c, void *stream) { (void)c; (void)stream; return c; }
#else
/* 64-bit: wire through to real libc stdio (cast FILE* to/from void*) */
WINE_STUB
int _m_fprintf(void *stream, const char *fmt, ...) {
    va_list ap;
    int ret;
    va_start(ap, fmt);
    ret = vfprintf((FILE *)stream, fmt, ap);
    va_end(ap);
    return ret;
}
WINE_STUB
int _m_fwrite(const void *ptr, size_t size, size_t nmemb, void *stream) {
    return fwrite(ptr, size, nmemb, (FILE *)stream);
}
WINE_STUB
int _m_vfprintf(void *stream, const char *fmt, void *ap) {
    return vfprintf((FILE *)stream, fmt, *(va_list *)ap);
}
WINE_STUB
int _m_fputc(int c, void *stream) {
    return fputc(c, (FILE *)stream);
}
#endif
