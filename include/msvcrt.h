#ifndef MY_WINE_MSVCRT_H
#define MY_WINE_MSVCRT_H

#include <stddef.h>
#include <stdint.h>

/* ── MSVCRT CRT Startup Stubs ──────────────────────────────── */

/* Global variables accessed by CRT startup code */
extern int __msvcrt_app_type;
extern int _commode;
extern int _fmode;
extern char **_msvcrt_environ;
extern char *_acmdln;

/* Guest argv/envp set from main.c before entry jump */
extern char **g_guest_argv;
extern char **g_guest_envp;

/* _cmdline_storage buffer and pointer - set from main.c before entry jump */
extern char _cmdline_storage[4096];

/* CRT startup functions */
__attribute__((ms_abi))
void __set_app_type(int type);
__attribute__((ms_abi))
void __getmainargs(int *argc, char ***argv, char ***envp, int expand_env, void *pStartInfo);
__attribute__((ms_abi))
void __initenv(void);
__attribute__((ms_abi))
void _initterm(void);
__attribute__((ms_abi))
void *_initterm_e(const void **pi, const void **pe);
__attribute__((ms_abi))
void *_onexit(void (*func)(void));
__attribute__((ms_abi))
void *__p__commode(void);
__attribute__((ms_abi))
void *__p__fmode(void);

/* IO buffers */
__attribute__((ms_abi))
void *__iob_func(void);

/* Locale */
__attribute__((ms_abi))
void __lconv_init(void);

/* Math error */
__attribute__((ms_abi))
void __setusermatherr(void (*handler)(void));

/* Stdlib functions */
__attribute__((ms_abi))
void _amsg_exit(int msg);
__attribute__((ms_abi))
void _cexit(void);
/* _exit and exit: main.c calls these directly with SysV ABI, so no ms_abi */
void _exit(int code);
void exit(int code);
/* abort: declared in stdlib.h, PE gets it via import table pointer */
__attribute__((ms_abi))
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
extern void *__msvcrt_memcpy;
extern void *__msvcrt_strlen;
extern void *__msvcrt_strncmp;
extern void *__msvcrt_exit;
extern void *__msvcrt__exit;
extern void *__msvcrt_abort;
extern void *__msvcrt_signal;

/* CRT context — image base and .bss VA */
typedef struct {
    uint64_t image_base;    /* Base address of the loaded PE image */
    uint32_t bss_vaddr;     /* VirtualAddress of the .bss section */
} crt_context_t;

extern crt_context_t g_crt_ctx;

/* Patch refptrs in the PE's .rdata to point to our globals */
#include "pe.h"
void patch_crt_refptrs(const char *file_path, void *image_base, IMAGE_NT_HEADERS64 *nt, IMAGE_SECTION_HEADER *sections);

#endif /* MY_WINE_MSVCRT_H */
