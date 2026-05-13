/*
 * crt_startup.c — CRT startup function stubs.
 *
 * Called by mainCRTStartup before reaching user's main().
 */

#define _GNU_SOURCE

#include "msvcrt_priv.h"

/* ── Data symbols and functions needed by import table (not in other msvcrt files) ── */
char **__initenv = NULL;

/* _errno — return errno pointer (crt_misc.c has _errno_func, not _errno) */
static int __my_wine_errno;
int *_errno(void)
{
    return &__my_wine_errno;
}

/* ── _m_* wrappers for import table (64-bit PE) ────────────── */
/*
 * PE code uses the Microsoft x64 calling convention.  These _m_* functions
 * are called directly from the PE, so they must declare ms_abi.  We avoid
 * calling libc functions (which use System V) by implementing the logic
 * inline or using GCC builtins.
 */
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdarg.h>
#include <locale.h>
#include <wchar.h>
#include <signal.h>

/* --- memory --- */
__attribute__((ms_abi)) void *_m_malloc(size_t size) { return malloc(size); }
__attribute__((ms_abi)) void _m_free(void *ptr) { free(ptr); }
__attribute__((ms_abi)) void *_m_calloc(size_t nmemb, size_t size) { return calloc(nmemb, size); }
__attribute__((ms_abi)) void *_m_realloc(void *ptr, size_t size) { return realloc(ptr, size); }
__attribute__((ms_abi)) void *_m_memcpy(void *dst, const void *src, size_t n) { return __builtin_memcpy(dst, src, n); }
__attribute__((ms_abi)) void *_m_memset(void *dst, int c, size_t n) { return __builtin_memset(dst, c, n); }

/* --- string --- */
__attribute__((ms_abi)) size_t _m_strlen(const char *s) { return __builtin_strlen(s); }
__attribute__((ms_abi)) int _m_strcmp(const char *a, const char *b) { return __builtin_strcmp(a, b); }
__attribute__((ms_abi)) int _m_strncmp(const char *a, const char *b, size_t n) { return __builtin_strncmp(a, b, n); }
__attribute__((ms_abi)) int _m_memcmp(const void *a, const void *b, size_t n) { return __builtin_memcmp(a, b, n); }

/* --- misc --- */
__attribute__((ms_abi)) void _m_abort(void) { abort(); }
__attribute__((ms_abi)) void _m_exit(int code) { exit(code); }
typedef void (*sig_handler_t)(int);
__attribute__((ms_abi)) sig_handler_t _m_signal(int sig, sig_handler_t handler) { return signal(sig, handler); }
__attribute__((ms_abi)) size_t _m_wcslen(const void *s) { return wcslen((const wchar_t *)s); }
__attribute__((ms_abi)) struct lconv *_m_localeconv(void) { return localeconv(); }
__attribute__((ms_abi)) char *_m_strerror(int n) { return strerror(n); }

/* --- stdio (varargs functions already work with ms_abi for the variadic part) */
__attribute__((ms_abi)) int _m_fprintf(void *stream, const char *fmt, ...) {
    va_list ap; va_start(ap, fmt); int r = vfprintf((FILE *)stream, fmt, ap); va_end(ap); return r;
}
__attribute__((ms_abi)) int _m_fwrite(const void *ptr, size_t size, size_t nmemb, void *stream) {
    return fwrite(ptr, size, nmemb, (FILE *)stream);
}
__attribute__((ms_abi)) int _m_vfprintf(void *stream, const char *fmt, void *ap) {
    return vfprintf((FILE *)stream, fmt, *(va_list *)ap);
}
__attribute__((ms_abi)) int _m_fputc(int c, void *stream) { return fputc(c, (FILE *)stream); }

WINE_STUB
void __set_app_type(int type)
{
    __msvcrt_app_type = type;
}

/* __initenv is a data symbol (char**), not a function — MSVCRT exports it as such.
 * Renamed the no-op stub to avoid conflict with the data variable below. */

WINE_STUB
void _initterm(void)
{
}

WINE_STUB
void *_initterm_e(const void **pi, const void **pe)
{
    if (pi) *pi = NULL;
    if (pe) *pe = NULL;
    return FORCE_PTR_RETURN(NULL);
}

WINE_STUB
void *_onexit(void (*func)(void))
{
    (void)func;
    return FORCE_PTR_RETURN(NULL);
}

/*
 * __p__commode and __p__fmode return pointers to the commode/fmode variables.
 * The PE code uses the return value (RAX) to write to these variables.
 * With our refptr patches, these may not be called, but we provide correct
 * implementations just in case.
 */
WINE_STUB
void *__p__commode(void)
{
    return FORCE_PTR_RETURN(&_commode);
}

WINE_STUB
void *__p__fmode(void)
{
    return FORCE_PTR_RETURN(&_fmode);
}

/* __getmainargs: return the actual argv/envp passed from main.c */
WINE_STUB
void __getmainargs(int *argc, char ***argv, char ***envp, int expand_env, void *pStartInfo)
{
    if (argc) *argc = 1;
    if (argv) *argv = g_guest_argv ? g_guest_argv : (char **)(uintptr_t)0;
    if (envp) *envp = g_guest_envp ? g_guest_envp : (char **)(uintptr_t)0;

    /* Also write to the PE's .bss section so the CRT can find them.
     * The .bss section VA is found dynamically via g_crt_ctx.bss_vaddr (set in patch_crt_refptrs).
     * The offsets come from COFF symbol lookup (with hardcoded fallback) in g_crt_ctx.
     * The CRT reads argv from this location and does two-level indirection: mov (%r13),%rcx
     * If argv is NULL there, dereferencing 0 → SIGSEGV. */
    uint64_t image_base = g_crt_ctx.image_base;
    if (image_base && g_crt_ctx.bss_vaddr != 0) {
        char *bss = (char *)image_base + g_crt_ctx.bss_vaddr;
        if (g_crt_ctx.argc_bss_offset)
            *(uint32_t *)(bss + g_crt_ctx.argc_bss_offset) = 1;
        /* Write pointer size matching the PE type: 4 bytes for PE32, 8 for PE32+ */
        if (g_crt_ctx.argv_bss_offset) {
            if (g_is_32bit) {
                *(uint32_t *)(bss + g_crt_ctx.argv_bss_offset) = (uint32_t)(uintptr_t)(g_guest_argv ? g_guest_argv : 0);
            } else {
                *(uint64_t *)(bss + g_crt_ctx.argv_bss_offset) = (uint64_t)(uintptr_t)(g_guest_argv ? g_guest_argv : 0);
            }
        }
        if (g_crt_ctx.envp_bss_offset) {
            if (g_is_32bit) {
                *(uint32_t *)(bss + g_crt_ctx.envp_bss_offset) = (uint32_t)(uintptr_t)(g_guest_envp ? g_guest_envp : 0);
            } else {
                *(uint64_t *)(bss + g_crt_ctx.envp_bss_offset) = (uint64_t)(uintptr_t)(g_guest_envp ? g_guest_envp : 0);
            }
        }
    }

    (void)expand_env;
    (void)pStartInfo;
}

WINE_STUB
void *_setargv(void)
{
    return FORCE_PTR_RETURN(NULL);
}

WINE_STUB
void __lconv_init(void)
{
}

WINE_STUB
void __setusermatherr(void (*handler)(void))
{
    (void)handler;
}
