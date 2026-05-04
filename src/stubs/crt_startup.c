/*
 * crt_startup.c — CRT startup function stubs.
 *
 * Called by mainCRTStartup before reaching user's main().
 */

#define _GNU_SOURCE

#include "msvcrt_priv.h"

WINE_STUB
void __set_app_type(int type)
{
    __msvcrt_app_type = type;
}

WINE_STUB
void __initenv(void)
{
}

WINE_STUB
void _initterm(void)
{
}

WINE_STUB
void *_initterm_e(const void **pi, const void **pe)
{
    if (pi) *pi = NULL;
    if (pe) *pe = NULL;
    return NULL;
}

WINE_STUB
void *_onexit(void (*func)(void))
{
    (void)func;
    return NULL;
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
    return &_commode;
}

WINE_STUB
void *__p__fmode(void)
{
    return &_fmode;
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
        if (g_crt_ctx.argv_bss_offset)
            *(uint64_t *)(bss + g_crt_ctx.argv_bss_offset) = (uint64_t)(uintptr_t)(g_guest_argv ? g_guest_argv : 0);
        if (g_crt_ctx.envp_bss_offset)
            *(uint64_t *)(bss + g_crt_ctx.envp_bss_offset) = (uint64_t)(uintptr_t)(g_guest_envp ? g_guest_envp : 0);
    }

    (void)expand_env;
    (void)pStartInfo;
}

WINE_STUB
void *_setargv(void)
{
    return NULL;
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
