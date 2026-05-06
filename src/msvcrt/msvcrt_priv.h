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

/* ── Global variables (defined in crt_globals.c) ───────────── */

extern int __msvcrt_app_type;
extern int _commode;
extern int _fmode;
extern char **_msvcrt_environ;

extern char **g_guest_argv;
extern char **g_guest_envp;

extern char _cmdline_storage[4096];
extern char *_acmdln;

extern uint64_t native_startup_lock;
extern int native_startup_state;
extern int dowildcard_val;
extern int newmode_val;
extern crt_context_t g_crt_ctx;

extern uint64_t dyn_tls_callback_stub;
extern uint64_t mingw_excpt_handler_stub;
extern uint64_t xc_a_stub;
extern uint64_t xc_z_stub;

extern uint32_t ctor_list_stub[];
extern uint32_t dtor_list_stub[];

extern uint64_t xi_a_stub;
extern uint64_t xi_z_stub;

extern void **__imp___initenv_stub;

/* ── FILE structures (defined in crt_file.c) ───────────── */

#define WINE_FILE_SIZE 48

#pragma pack(push, 1)
typedef struct {
    int            _fd;
    unsigned char *_ptr;
    int            _cnt;
    unsigned char *_base;
    uintptr_t      _flag;
    unsigned char  _pad[16];
} wine_FILE;
#pragma pack(pop)

_Static_assert(sizeof(wine_FILE) == WINE_FILE_SIZE, "wine_FILE size mismatch");

#define WINE_IOEOF  0x8000
#define WINE_IOWRT  0x0002
#define WINE_IONBF  0x4000
#define WINE_IOREAD 0x0001
#define WINE_IOFBF  0x0200

typedef union {
    wine_FILE f[3];
    char      bytes[48 * 3];
} iob_union;

extern iob_union __wine_iob;

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

typedef struct {
    const char *name;
    void       *target;
} refptr_mapping_t;

extern const refptr_mapping_t refptr_mappings[];
#define REF_MAP_COUNT (sizeof(refptr_mappings) / sizeof(refptr_mappings[0]) - 1)

void patch_crt_refptrs(const char *file_path, void *image_base,
                       IMAGE_NT_HEADERS64 *nt,
                       IMAGE_SECTION_HEADER *sections);
void apply_refptr_patch(void *image_base, uint64_t rva, void *target,
                        const char *name, uint64_t image_size);

/* ── CRT offset discovery (defined in crt_offset_discovery.c) ── */
void discover_crt_offsets(const char *file_path,
                          IMAGE_NT_HEADERS64 *nt,
                          IMAGE_SECTION_HEADER *sections);
uint64_t find_symbol_rva_from_file(const char *file_path,
                                   IMAGE_NT_HEADERS64 *nt,
                                   IMAGE_SECTION_HEADER *sections,
                                   const char *name);
void scan_text_for_refptrs(void *image_base,
                           IMAGE_NT_HEADERS64 *nt,
                           IMAGE_SECTION_HEADER *sections,
                           uint64_t image_size);

#endif /* MSVCRT_PRIV_H */
