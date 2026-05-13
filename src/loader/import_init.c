/*
 * import_init.c — msvcrt dynamic import initialization
 *
 * Fills NULL entries in import_table for dynamically-resolved msvcrt symbols.
 */

#include <stdio.h>
#include <string.h>

#include "loader_priv.h"

#ifndef MY_WINE_32
#include "../msvcrt/msvcrt_priv.h"
#endif

/* Forward declarations for data import targets */
extern void *__wine_iob_data(void);

/* Wrapper function declarations (from crt_32_stub.c or crt_startup.c) */
extern char *__p__acmdln_func(void);
extern char *__p__fmode_func(void);
extern char *__p__commode_func(void);

/**
 * Fill dynamic msvcrt import entries (abort, malloc, etc.) with
 * pointers to our internal implementations.
 */
void init_msvcrt_imports(void)
{
#ifndef MY_WINE_32
    /* 64-bit build: fill dynamic msvcrt entries with CRT stub addresses.
     * These __msvcrt_* symbols come from crt_*.c which is excluded from
     * the 32-bit build. */
    set_import("abort",    __msvcrt_abort);
    set_import("calloc",   __msvcrt_calloc);
    set_import("exit",     __msvcrt_exit);
    set_import("fprintf",  __msvcrt_fprintf);
    set_import("free",     __msvcrt_free);
    set_import("fwrite",   __msvcrt_fwrite);
    set_import("malloc",   __msvcrt_malloc);
    set_import("memcpy",   __msvcrt_memcpy);
    set_import("realloc",  __msvcrt_realloc);
    set_import("signal",   __msvcrt_signal);
    set_import("strlen",   __msvcrt_strlen);
    set_import("strncmp",  __msvcrt_strncmp);
    set_import("vfprintf", __msvcrt_vfprintf);
    set_import("___lc_codepage_func", __msvcrt___lc_codepage_func);
    set_import("___mb_cur_max_func",  __msvcrt___mb_cur_max_func);
    set_import("_errno",    __msvcrt__errno_func);
    set_import("_lock",     __msvcrt__lock);
    set_import("_unlock",   __msvcrt__unlock);
    set_import("fputc",     __msvcrt_fputc);
    set_import("localeconv", __msvcrt_localeconv);
    set_import("strerror",  __msvcrt_strerror);
    set_import("wcslen",    __msvcrt_wcslen);

    /* Data imports: use wrapper functions for __p__* (JMP thunks),
     * keep __initenv as data (PE writes to it, not calls it). */
    set_import("__p__acmdln",   (void *)__p__acmdln_func);
    set_import("__p__commode",  (void *)__p__commode_func);
    set_import("__p__fmode",    (void *)__p__fmode_func);
    set_import("_iob",          __wine_iob_data);
#else
    /* 32-bit build: CRT stubs (crt_*.c) are excluded.
     * The static msvcrt entries in import_table.c (__C_specific_handler,
     * __getmainargs, __iob_func, etc.) already have handler addresses.
     * Dynamic msvcrt entries remain NULL and are skipped by resolve_imports.
     * The ntdll/kernel32 entries have handler addresses from the stubs. */
#endif
}
