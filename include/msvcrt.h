#ifndef MY_WINE_MSVCRT_H
#define MY_WINE_MSVCRT_H

#include "wine_abi.h"
#include <stddef.h>
#include <stdint.h>
#include "common.h"
#include "crt.h"

/* ── MSVCRT CRT Startup Stubs ──────────────────────────────── */

/*
 * All CRT global state is now in g_crt (wine_crt_state_t, defined in include/crt.h).
 * Access via g_crt.app_type, g_crt.commode, g_crt.fmode, g_crt.environ,
 * g_crt.acmdln, g_crt.guest_argv, g_crt.guest_envp, g_crt.cmdline_storage,
 * g_crt.initenv, etc.
 */

/* CRT startup functions */
GUEST_ABI
void __set_app_type(int type);
GUEST_ABI
void __getmainargs(int *argc, char ***argv, char ***envp, int expand_env, void *pStartInfo);
#ifdef MY_WINE32
/* 32-bit: these are data symbols (from crt_32_stub.c) */
extern char **__initenv;
extern char *__p__acmdln;
extern char *__p__commode;
extern char *__p__fmode;
#else
/* 64-bit: __p__commode/__p__fmode are function stubs (from crt_startup.c).
 * __initenv is now g_crt.initenv (in g_crt, from include/crt.h). */
GUEST_ABI
void *__p__commode(void);
GUEST_ABI
void *__p__fmode(void);
#endif
GUEST_ABI
void _initterm(void);
GUEST_ABI
void *_initterm_e(const void **pi, const void **pe);
GUEST_ABI
void *_onexit(void (*func)(void));

/* IO buffers */
GUEST_ABI
void *__iob_func(void);
GUEST_ABI
void *__acrt_iob_func(void);

/* Locale */
GUEST_ABI
void __lconv_init(void);

/* Math error */
GUEST_ABI
void __setusermatherr(void (*handler)(void));

/* Stdlib functions */
GUEST_ABI
void _amsg_exit(int msg);
GUEST_ABI
void _cexit(void);
/* _exit and exit: main.c calls these directly with SysV ABI, so no ms_abi */
void _exit(int code);
void exit(int code);
/* abort: declared in stdlib.h, PE gets it via import table pointer */
GUEST_ABI
void *_setargv(void);

/* Memory functions */
/* malloc/calloc/free/memcpy/strlen/strncmp: main.c calls these directly with SysV ABI */
void *malloc(size_t size);
void *calloc(size_t nmemb, size_t size);
void free(void *ptr);
void *memcpy(void *dest, const void *src, size_t n);

/* String functions */
size_t strlen(const char *s);
int strncmp(const char *s1, const char *s2, size_t n);

/*
 * Functions that shadow host libc (fprintf, malloc, exit, etc.) are
 * defined in msvcrt.c but NOT declared here to avoid conflicts with
 * host stdio.h/stdlib.h. The import table in my_wine.c references
 * them via extern void* pointers (see the __msvcrt_* symbols below).
 */

/* Extern pointers for host-libc-shading symbols (used in import table) */
extern void *__msvcrt_fprintf;
extern void *__msvcrt_fwrite;
extern void *__msvcrt_vfprintf;
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

#endif /* MY_WINE_MSVCRT_H */
