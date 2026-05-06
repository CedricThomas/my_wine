/*
 * import_init.c — msvcrt dynamic import initialization
 *
 * Fills NULL entries in import_table for dynamically-resolved msvcrt symbols.
 */

#include <stdio.h>
#include <string.h>

#include "loader_priv.h"
#include "../stubs/msvcrt_priv.h"

/**
 * Fill dynamic msvcrt import entries (abort, malloc, etc.) with
 * pointers to our internal implementations.
 */
void init_msvcrt_imports(void)
{
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
}
