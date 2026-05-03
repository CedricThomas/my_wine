/*
 * crt_file.c — Fake FILE structures for __iob_func.
 *
 * The PE's __acrt_iob_func indexes into an array of 48-byte FILE structs.
 * We provide three inline structs (stdin, stdout, stderr) contiguously.
 */

#define _GNU_SOURCE

#include <stdio.h>
#include "msvcrt_priv.h"

/* ── Fake FILE structures for __iob_func ──────────────────── */

iob_union __wine_iob = {
    .f[0] = { ._fd = 0, ._flag = (uintptr_t)(WINE_IOREAD | WINE_IONBF) },
    .f[1] = { ._fd = 1, ._flag = (uintptr_t)(WINE_IOWRT  | WINE_IONBF) },
    .f[2] = { ._fd = 2, ._flag = (uintptr_t)(WINE_IOWRT  | WINE_IONBF) },
};

/* Accessor for use from main.c to patch __acrt_iob_func */
void *__wine_iob_data(void)
{
    return __wine_iob.bytes;
}

/*
 * __iob_func: returns the base of the FILE array.
 * The PE's __acrt_iob_func does:
 *   mov %ecx,%ebx     ; save index
 *   call __iob_func   ; we read ebx from stack frame
 *   mov %ebx,%ecx     ; restore index (but rcx upper bits are garbage)
 *   [broken math using rcx]
 *   add %rdx,%rax
 *
 * We return the base pointer; the subsequent patching in entry.c
 * replaces the broken math with a direct return of __wine_iob_data.
 */
WINE_STUB
void *__iob_func(void)
{
    return __wine_iob.bytes;
}
