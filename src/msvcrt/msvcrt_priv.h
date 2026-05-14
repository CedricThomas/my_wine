/*
 * msvcrt_priv.h — Private declarations shared between msvcrt split files.
 *
 * Contains: all global variable externs, wine_* internal function declarations,
 * __msvcrt_* function pointer externs, refptr mapping definitions, and
 * patch_crt_refptrs declaration.
 *
 * Include this in all crt_*.c files instead of include/msvcrt.h.
 */

#ifndef MSVCRT_PRIV_H
#define MSVCRT_PRIV_H

#include <stddef.h>
#include <stdarg.h>
#include <stdint.h>
#include "include/msvcrt.h"
#include "include/wine_abi.h"
#include "include/pe.h"
#include "include/pe_parser.h"
#include "include/common.h"
#include "include/crt.h"

/* ── wine_crt_state_t, g_crt, wine_FILE, iob_union ─────────────
 * All defined in include/crt.h (included above via msvcrt.h → crt.h).
 * g_crt is defined in crt_globals.c.
 * SINGLE-THREAD ONLY: g_crt is not safe for concurrent access.
 */

/* ── Internal wine_* functions (defined in crt_stdio.c / crt_stdlib.c) ── */
/*
 * Guarded by #ifdef so each .c file only sees declarations for functions
 * it actually defines, suppressing -Wunused-function in files that don't.
 */

#ifdef CRT_STDIO_C
WINE_STUB_STATIC
int wine_vfprintf(wine_FILE *stream, const char *format, va_list ap);
WINE_STUB_STATIC
int wine_fprintf(wine_FILE *stream, const char *format, ...);
WINE_STUB_STATIC
size_t wine_fwrite(const void *ptr, size_t size, size_t nmemb, wine_FILE *stream);
#endif

#ifdef CRT_STDLIB_C
WINE_STUB_STATIC
void wine__exit(int code);
WINE_STUB_STATIC
void wine_abort(void);
WINE_STUB_STATIC
int wine_exit(int code);
WINE_STUB_STATIC
void *wine_malloc(size_t size);
WINE_STUB_STATIC
void *wine_calloc(size_t nmemb, size_t size);
WINE_STUB_STATIC
void wine_free(void *ptr);
WINE_STUB_STATIC
void *wine_realloc(void *ptr, size_t size);
WINE_STUB_STATIC
void *wine_memcpy(void *dest, const void *src, size_t n);
WINE_STUB_STATIC
size_t wine_strlen(const void *s);
WINE_STUB_STATIC
int wine_strncmp(const void *s1, const void *s2, size_t n);
WINE_STUB_STATIC
int wine_signal(int sig, void (*handler)(int));
#endif

/* ── __msvcrt_* function pointer exports ─────────────────── */

/* Defined in crt_stdio.c */
extern void *__msvcrt_fprintf;
extern void *__msvcrt_fwrite;
extern void *__msvcrt_vfprintf;

/* Defined in crt_stdlib.c */
extern void *__msvcrt_malloc;
extern void *__msvcrt_calloc;
extern void *__msvcrt_free;
extern void *__msvcrt_realloc;
extern void *__msvcrt_memcpy;
extern void *__msvcrt_strlen;
extern void *__msvcrt_strncmp;
extern void *__msvcrt_exit;
extern void *__msvcrt__exit;
extern void *__msvcrt_abort;
extern void *__msvcrt_signal;

/* Additional CRT stubs (defined in crt_misc.c) */
extern void *__msvcrt___lc_codepage_func;
extern void *__msvcrt___mb_cur_max_func;
extern void *__msvcrt__errno_func;
extern void *__msvcrt__lock;
extern void *__msvcrt__unlock;
extern void *__msvcrt_fputc;
extern void *__msvcrt_localeconv;
extern void *__msvcrt_strerror;
extern void *__msvcrt_wcslen;

/* ── Refptr patching (defined in crt_refptrs.c) ─────────── */

void patch_crt_refptrs(const char *file_path, void *image_base,
                       IMAGE_NT_HEADERS *nt,
                       IMAGE_SECTION_HEADER *sections);
void apply_refptr_patch(void *image_base, uint64_t rva, void *target,
                        const char *name, uint64_t image_size,
                        uint64_t ctx_image_base, uint64_t ctx_bss_vaddr,
                        IMAGE_NT_HEADERS *nt,
                        IMAGE_SECTION_HEADER *sections);

/* ── CRT offset discovery (defined in crt_offset_discovery.c) ── */
void discover_crt_offsets(const char *file_path,
                          IMAGE_NT_HEADERS *nt,
                          IMAGE_SECTION_HEADER *sections,
                          crt_context_t *ctx);
uint64_t find_symbol_rva_from_file(const char *file_path,
                                   IMAGE_NT_HEADERS *nt,
                                   IMAGE_SECTION_HEADER *sections,
                                   const char *name);
void scan_text_for_refptrs(void *image_base,
                           IMAGE_NT_HEADERS *nt,
                           IMAGE_SECTION_HEADER *sections,
                           uint64_t image_size, void *initenv_stub);

#endif /* MSVCRT_PRIV_H */
