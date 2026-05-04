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
    set_import("signal",   __msvcrt_signal);
    set_import("strlen",   __msvcrt_strlen);
    set_import("strncmp",  __msvcrt_strncmp);
    set_import("vfprintf", __msvcrt_vfprintf);
}
