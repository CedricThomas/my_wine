/*
 * msvcrt.c — MSVCRT DLL stub implementations
 *
 * Provides the CRT startup functions and standard library functions
 * needed by mingw-w64 compiled programs. These are called by
 * mainCRTStartup before reaching user's main().
 *
 * Functions that would shadow host libc names (fprintf, malloc, etc.)
 * use internal names prefixed with wine_ and are exposed via __msvcrt_*
 * function pointers for the import table.
 */

#define _GNU_SOURCE

#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <stdarg.h>
#include <unistd.h>
#include <signal.h>

/* We avoid stdio.h/stdlib.h to prevent name conflicts with our stubs.
 * Use __builtin_ functions and direct syscalls instead. */

#include "include/msvcrt.h"

/* ── Global variables ──────────────────────────────────────── */

int __msvcrt_app_type = 0;
int _commode = 0;
int _fmode = 0;
char **_msvcrt_environ = NULL;

/* Set from main.c before jump_to_entry; used by __getmainargs and _acmdln */
char **g_guest_argv  = NULL;
char **g_guest_envp  = NULL;

char _cmdline_storage[4096];  /* filled from g_guest_argv[0] in main.c */
char *_acmdln = _cmdline_storage;

/* Static variables for additional CRT refptr patches */
static int native_startup_lock = 0;
static int native_startup_state = 0;
static int dowildcard_val = 0;
static int newmode_val = 0;
static uint64_t g_image_base_ref = 0;  // will be set to image base at runtime

/* .bss section VirtualAddress — set dynamically by patch_crt_refptrs */
uint32_t g_bss_vaddr = 0;

/*
 * Zero-valued stubs for two-level refptrs.
 * The CRT startup code does two-level indirection:
 *   mov refptr(%rip), %rax   ; loads the value we store here
 *   mov (%rax), %rax          ; dereferences that value
 *
 * If we write 0 directly into the refptr, the second mov dereferences
 * address 0 → SIGSEGV. Instead, we store a pointer to a real global
 * that contains 0. The CRT reads our stub's address, dereferences it,
 * gets 0, and proceeds safely.
 */
static uint64_t dyn_tls_callback_stub     = 0;
static uint64_t mingw_excpt_handler_stub  = 0;
static uint64_t xc_a_stub                 = 0;
static uint64_t xc_z_stub                 = 0;

/*
 * Stub arrays for __CTOR_LIST__ / __DTOR_LIST__.
 * __do_global_ctors reads the first element: if 0, no constructors exist.
 * A single {0} entry means "empty list" — the CRT skips the loop.
 */
static uint32_t ctor_list_stub[] = { 0 };
static uint32_t dtor_list_stub[] = { 0 };

/*
 * __xi_a / __xi_z mark the constructor range. When equal, no constructors.
 * The CRT compares them: if __xi_a == __xi_z, skip __do_global_ctors.
 */
static uint64_t xi_a_stub = 0;
static uint64_t xi_z_stub = 0;

/*
 * __imp___initenv stub: the PE code does mov %r8,(%rax) to store envp.
 * This means __imp___initenv must point to a writable location where
 * envp is stored. We set this dynamically to point to PE's .bss envp.
 * Initialize to 0, fix up in patch_crt_refptrs.
 */
static void **__imp___initenv_stub = NULL;  /* set dynamically */

/* ── Fake FILE structures for __iob_func ──────────────────── */

/*
 * The PE's __acrt_iob_func does:
 *   rdx = index * 3
 *   rdx = rdx * 16        ; rdx = index * 48
 *   rax += rdx            ; rax = iob_array + index*48
 *
 * So it expects __iob_func to return a pointer to an array of inline
 * FILE structs, each exactly 48 bytes. wine_vfprintf reads _fd at
 * offset 0x58, so we need to pad the struct to 88+ bytes BUT only use
 * 48 bytes per entry in the array.
 *
 * Resolution: Make the struct exactly 48 bytes with _fd at offset 0.
 * Update wine_vfprintf to read _fd from offset 0 instead of 0x58.
 * Store three inline structs contiguously. __iob_func returns the
 * pointer to the array.
 */
#define WINE_FILE_SIZE 48

#pragma pack(push, 1)
typedef struct {
    int            _fd;        /* 0   - file descriptor */
    unsigned char *_ptr;       /* 8   - read/write pointer */
    int            _cnt;       /* 16  - bytes available */
    unsigned char *_base;      /* 24  - buffer base */
    uintptr_t      _flag;      /* 32  - flags (IOREAD, IOWRT, etc.) */
    unsigned char  _pad[16];   /* 40  - padding to 48 bytes */
} wine_FILE;
#pragma pack(pop)

#define WINE_IOEOF  0x8000
#define WINE_IOWRT  0x0002
#define WINE_IONBF  0x4000
#define WINE_IOREAD 0x0001
#define WINE_IOFBF  0x0200

/*
 * Each wine_FILE is exactly 48 bytes. We allocate them as individual
 * static variables at known offsets. __iob_func returns a pointer
 * so that __acrt_iob_func can index with iob_base + idx*48.
 *
 * Alternative: return a base address that is 48 bytes BEFORE _wine_stdout,
 * so that base + 48 = &_wine_stdout. But we also need base + 0 = &_wine_stdin.
 *
 * The only reliable solution: make the structs contiguous. Use a union
 * or array with explicit 48-byte elements.
 */
typedef union {
    wine_FILE f[3];
    char      bytes[48 * 3];
} iob_union;

static iob_union __wine_iob = {
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
 * But we make it a naked function that reads the index from the
 * saved ebx on the stack and returns the correct FILE* directly,
 * bypassing the broken offset computation in __acrt_iob_func.
 *
 * The PE's __acrt_iob_func does:
 *   mov %ecx,%ebx     ; save index
 *   call __iob_func   ; we read ebx from stack frame
 *   mov %ebx,%ecx     ; restore index (but rcx upper bits are garbage)
 *   [broken math using rcx]
 *   add %rdx,%rax
 *
 * Instead, we read the saved ebx (index) from our stack frame,
 * compute base + index*48, and return it directly in rax.
 * This way the PE's subsequent math (garbage*48 + our_return) is bypassed
 * because we've already returned the correct address.
 *
 * Wait - that doesn't help. The PE still does add%rdx,%rax after the call.
 *
 * Real fix: replace the 13 bytes AFTER the call with code that ignores
 * the garbage and uses the saved ebx directly.
 */

/* __iob_func returns pointer to the contiguous array of FILE structs */
__attribute__((ms_abi, force_align_arg_pointer))
void *__iob_func(void)
{
    return __wine_iob.bytes;
}

/* ── CRT Startup Functions ────────────────────────────────── */

__attribute__((ms_abi, force_align_arg_pointer))
void __set_app_type(int type)
{
    __msvcrt_app_type = type;
}

__attribute__((ms_abi, force_align_arg_pointer))
void __initenv(void)
{
}

__attribute__((ms_abi, force_align_arg_pointer))
void _initterm(void)
{
}

__attribute__((ms_abi, force_align_arg_pointer))
void *_initterm_e(const void **pi, const void **pe)
{
    if (pi) *pi = NULL;
    if (pe) *pe = NULL;
    return NULL;
}

__attribute__((ms_abi, force_align_arg_pointer))
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
__attribute__((ms_abi, force_align_arg_pointer))
void *__p__commode(void)
{
    return &_commode;
}

__attribute__((ms_abi, force_align_arg_pointer))
void *__p__fmode(void)
{
    return &_fmode;
}

/* __getmainargs: return the actual argv/envp passed from main.c */
__attribute__((ms_abi, force_align_arg_pointer))
void __getmainargs(int *argc, char ***argv, char ***envp, int expand_env, void *pStartInfo)
{
    if (argc) *argc = 1;
    if (argv) *argv = g_guest_argv ? g_guest_argv : (char **)(uintptr_t)0;
    if (envp) *envp = g_guest_envp ? g_guest_envp : (char **)(uintptr_t)0;

    /* Also write to the PE's .bss section so the CRT can find them.
     * The .bss section VA is found dynamically via g_bss_vaddr (set in patch_crt_refptrs).
     *   argc at +0x028 (4 bytes), argv at +0x020 (8 bytes), envp at +0x018 (8 bytes)
     * These relative offsets are mingw-w64 CRT-specific and ideally would come from
     * the symbol table, but they are linker-defined for the CRT startup layout.
     * The CRT reads argv from this location and does two-level indirection: mov (%r13),%rcx
     * If argv is NULL there, dereferencing 0 → SIGSEGV. */
    uint64_t image_base = g_image_base_ref;
    if (image_base && g_bss_vaddr != 0) {
        char *bss = (char *)image_base + g_bss_vaddr;
        *(uint32_t *)(bss + 0x028) = 1;            // argc = 1
        *(uint64_t *)(bss + 0x020) = (uint64_t)(uintptr_t)(g_guest_argv ? g_guest_argv : 0);  // argv
        *(uint64_t *)(bss + 0x018) = (uint64_t)(uintptr_t)(g_guest_envp ? g_guest_envp : 0);  // envp
    }

    (void)expand_env;
    (void)pStartInfo;
}

__attribute__((ms_abi, force_align_arg_pointer))
void *_setargv(void)
{
    return NULL;
}

__attribute__((ms_abi, force_align_arg_pointer))
void __lconv_init(void)
{
}

__attribute__((ms_abi, force_align_arg_pointer))
void __setusermatherr(void (*handler)(void))
{
    (void)handler;
}

/* ── Forward declarations for internal functions ───────────── */

static __attribute__((ms_abi, force_align_arg_pointer))
void wine__exit(int code);
static __attribute__((ms_abi, force_align_arg_pointer))
int wine_vfprintf(wine_FILE *stream, const char *format, va_list ap);

/* ── Stdlib Stubs ─────────────────────────────────────────── */

__attribute__((ms_abi, force_align_arg_pointer))
void _amsg_exit(int msg)
{
    /* Trace: _amsg_exit called with msg=%d */
    char buf[64];
    int len = 0;
    const char p[] = "amsg_exit(msg=";
    for (int i = 0; p[i]; i++) buf[len++] = p[i];
    int tmp = msg < 0 ? -msg : msg;
    if (msg < 0) buf[len++] = '-';
    char digits[16];
    int di = 0;
    do { digits[di++] = '0' + (tmp % 10); tmp /= 10; } while (tmp > 0);
    while (di > 0) buf[len++] = digits[--di];
    buf[len++] = ')';
    buf[len++] = '\n';
    buf[len] = 0;
    long a = 1;
    __asm__ volatile("syscall"
        : "+a"(a) : "D"(2), "S"(buf), "d"(len) : "rcx","r11","memory","cc");
    wine__exit(1);
}

__attribute__((ms_abi, force_align_arg_pointer))
void _cexit(void)
{
    wine__exit(0);
}

/* ── Internal implementations with unique names ────────────── */

static __attribute__((ms_abi, force_align_arg_pointer))
void wine__exit(int code)
{
    /* Call Linux sys_exit directly */
    syscall(60, code);
    __builtin_unreachable();
}

static __attribute__((ms_abi, force_align_arg_pointer))
void wine_abort(void)
{
    /* Dump registers to stack, then write them via syscall */
    char buf[512];
    int pos = 0;
    const char *hex = "0123456789abcdef";
    
    /* "ABORT: " */
    const char h0[] = "ABORT: ";
    memcpy(buf+pos, h0, 7); pos += 7;
    
    /* Get RSP before we mess with anything */
    uintptr_t rsp_val;
    __asm__ volatile("mov %%rsp, %0" : "=r"(rsp_val));
    
    /* Read return address from [rsp] */
    uintptr_t ret_addr;
    __asm__ volatile("movq (%%rsp), %0" : "=&r"(ret_addr));
    
    /* Read RBP */  
    uintptr_t rbp_val;
    __asm__ volatile("mov %%rbp, %0" : "=r"(rbp_val));
    
    /* Print "RSP=0x..." */
    const char t1[] = "RSP=0x";
    memcpy(buf+pos, t1, 6); pos += 6;
    { uintptr_t v = rsp_val; int i; for(i=60;i>=0;i-=4) buf[pos++]=hex[(v>>i)&0xf]; buf[pos++]=' '; buf[pos++]='\0'; }
    /* Hmm, need to print incrementally... let's use a helper */
    
    /* Simpler: just print the key values via separate syscalls */
    /* Return address */
    {
        char h[16] = "RET=";
        int p = 4;
        uintptr_t v = ret_addr;
        for (int i = 60; i >= 0; i -= 4) h[p++] = hex[(v >> i) & 0xf];
        h[p++] = '\n'; h[p] = '\0';
        long rr = 1;
        __asm__ volatile("syscall" : "+a"(rr) : "D"(2), "S"(h), "d"(p) : "rcx","r11","memory","cc");
    }
    
    /* RSP */
    {
        char h[16] = "RSP=";
        int p = 4;
        uintptr_t v = rsp_val;
        for (int i = 60; i >= 0; i -= 4) h[p++] = hex[(v >> i) & 0xf];
        h[p++] = '\n'; h[p] = '\0';
        long rr = 1;
        __asm__ volatile("syscall" : "+a"(rr) : "D"(2), "S"(h), "d"(p) : "rcx","r11","memory","cc");
    }
    
    /* RBP */
    {
        char h[16] = "RBP=";
        int p = 4;
        uintptr_t v = rbp_val;
        for (int i = 60; i >= 0; i -= 4) h[p++] = hex[(v >> i) & 0xf];
        h[p++] = '\n'; h[p] = '\0';
        long rr = 1;
        __asm__ volatile("syscall" : "+a"(rr) : "D"(2), "S"(h), "d"(p) : "rcx","r11","memory","cc");
    }
    
    /* Dump 8 stack values from rsp */
    {
        char h[16] = "SP0=";
        int p = 4;
        uintptr_t v = ret_addr;  /* [rsp] = return address */
        for (int i = 60; i >= 0; i -= 4) h[p++] = hex[(v >> i) & 0xf];
        h[p++] = '\n'; h[p] = '\0';
        long rr = 1;
        __asm__ volatile("syscall" : "+a"(rr) : "D"(2), "S"(h), "d"(p) : "rcx","r11","memory","cc");
    }
    
    /* Read [rsp+8] */
    { uintptr_t v2; __asm__ volatile("movq 8(%%rsp), %0" : "=&r"(v2));
      char h[16] = "SP1="; int p = 4;
      for (int i = 60; i >= 0; i -= 4) h[p++] = hex[(v2 >> i) & 0xf];
      h[p++] = '\n'; h[p] = '\0';
      long rr = 1;
      __asm__ volatile("syscall" : "+a"(rr) : "D"(2), "S"(h), "d"(p) : "rcx","r11","memory","cc");
    }
    
    /* Read [rsp+16] */
    { uintptr_t v2; __asm__ volatile("movq 16(%%rsp), %0" : "=&r"(v2));
      char h[16] = "SP2="; int p = 4;
      for (int i = 60; i >= 0; i -= 4) h[p++] = hex[(v2 >> i) & 0xf];
      h[p++] = '\n'; h[p] = '\0';
      long rr = 1;
      __asm__ volatile("syscall" : "+a"(rr) : "D"(2), "S"(h), "d"(p) : "rcx","r11","memory","cc");
    }

    wine__exit(134);
}

static __attribute__((ms_abi, force_align_arg_pointer))
int wine_exit(int code)
{
    wine__exit(code);
    __builtin_unreachable();
}

static __attribute__((ms_abi, force_align_arg_pointer))
void *wine_malloc(size_t size)
{
    return __builtin_malloc(size);
}

static __attribute__((ms_abi, force_align_arg_pointer))
void *wine_calloc(size_t nmemb, size_t size)
{
    return __builtin_calloc(nmemb, size);
}

static __attribute__((ms_abi, force_align_arg_pointer))
void wine_free(void *ptr)
{
    __builtin_free(ptr);
}

static __attribute__((ms_abi, force_align_arg_pointer))
void *wine_memcpy(void *dest, const void *src, size_t n)
{
    return __builtin_memcpy(dest, src, n);
}

static __attribute__((ms_abi, force_align_arg_pointer))
size_t wine_strlen(const void *s)
{
    return __builtin_strlen(s);
}

static __attribute__((ms_abi, force_align_arg_pointer))
int wine_strncmp(const void *s1, const void *s2, size_t n)
{
    return strncmp(s1, s2, n);
}

static __attribute__((ms_abi, force_align_arg_pointer))
int wine_fprintf(wine_FILE *stream, const char *format, ...)
{
    va_list ap;
    va_start(ap, format);
    int ret = wine_vfprintf(stream, format, ap);
    va_end(ap);
    return ret;
}

static __attribute__((ms_abi, force_align_arg_pointer))
int wine_vfprintf(wine_FILE *stream, const char *format, va_list ap)
{
    if (stream == NULL) return -1;
    uintptr_t base = (uintptr_t)__wine_iob.bytes;
    uintptr_t addr = (uintptr_t)stream;
    if (addr < base || addr >= base + WINE_FILE_SIZE * 3) return -1;
    int fd = stream->_fd;
    if (fd < 0 || fd > 2) return -1;
    (void)ap;  /* avoid compiler warning; we don't dereference garbage va_list */

    /* Instead of calling vsnprintf (which crashes on garbage va_list from PE),
     * write the format string directly. This handles most CRT startup output. */
    size_t len = 0;
    while (len < 4095 && format[len]) len++;
    if (len == 0) return 0;

    long res;
    __asm__ volatile("syscall"
                     : "=a"(res)
                     : "a"(1), "D"(fd), "S"(format), "d"(len)
                     : "rcx", "r11", "memory", "cc");
    (void)res;
    return (int)len;
}

static __attribute__((ms_abi, force_align_arg_pointer))
size_t wine_fwrite(const void *ptr, size_t size, size_t nmemb, wine_FILE *stream)
{
    if (stream == NULL) return 0;
    uintptr_t base = (uintptr_t)__wine_iob.bytes;
    uintptr_t addr = (uintptr_t)stream;
    if (addr < base || addr >= base + WINE_FILE_SIZE * 3) return 0;
    int fd = stream->_fd;
    if (fd < 0 || fd > 2) return 0;

    size_t total = size * nmemb;
    /* Use syscall directly to avoid callee-save SSE spills */
    long res;
    __asm__ volatile("syscall"
                     : "=a"(res)
                     : "a"(1), "D"(fd), "S"(ptr), "d"(total)
                     : "rcx", "r11", "memory", "cc");
    if (res < 0) return 0;
    return (size_t)res / size;
}

static __attribute__((ms_abi, force_align_arg_pointer))
void wine_signal(int sig, void (*handler)(int))
{
    signal(sig, handler);
}

/* ── Expose function pointers for import table ─────────────── */

void *__msvcrt_fprintf    = (void *)wine_fprintf;
void *__msvcrt_fwrite     = (void *)wine_fwrite;
void *__msvcrt_vfprintf   = (void *)wine_vfprintf;
void *__msvcrt_malloc     = (void *)wine_malloc;
void *__msvcrt_calloc     = (void *)wine_calloc;
void *__msvcrt_free       = (void *)wine_free;
void *__msvcrt_memcpy     = (void *)wine_memcpy;
void *__msvcrt_strlen     = (void *)wine_strlen;
void *__msvcrt_strncmp    = (void *)wine_strncmp;
void *__msvcrt_exit       = (void *)wine_exit;
void *__msvcrt__exit      = (void *)wine__exit;
void *__msvcrt_abort      = (void *)wine_abort;
void *__msvcrt_signal     = (void *)wine_signal;

/* ── Patch CRT refptrs ─────────────────────────────────────── */
#include <sys/mman.h>
#include "include/pe.h"
#include "include/pe_parser.h"

/*
 * The PE's .rdata contains .refptr entries that the CRT startup code
 * dereferences to find global variables like mingw_app_type, _fmode,
 * _commode, etc. These entries are initialized by the linker with
 * placeholder values that may not be valid addresses.
 *
 * We dynamically locate each .refptr entry using the COFF symbol table
 * when available, or fall back to known relative offsets within the
 * .refptr section.
 */

typedef struct {
    const char *name;     /* Symbol name to look up in COFF table */
    void       *target;   /* What the refptr should point to */
    uint32_t    rel_offset; /* Fallback: offset from .refptr section base */
} refptr_mapping_t;

/*
 * Name-to-target mappings for known CRT .refptr symbols.
 * The rel_offset is the offset from the .refptr section VirtualAddress,
 * derived from the original hardcoded RVAs (section base was 0x4300).
 * These offsets are linker-dependent but far more portable than absolute RVAs
 * since they work with any image base and section placement.
 */
static const refptr_mapping_t refptr_mappings[] = {
    { "__CTOR_LIST__",              (void *)&ctor_list_stub,              0x000 },
    { "__DTOR_LIST__",              (void *)&dtor_list_stub,              0x010 },
    { "__xi_a",                     (void *)&xi_a_stub,                   0x020 },
    { "__dyn_tls_init_callback",    (void *)&dyn_tls_callback_stub,       0x030 },
    { "__image_base__",             (void *)&g_image_base_ref,            0x040 },
    { "__imp___initenv",            (void *)&__imp___initenv_stub,        0x050 },
    { "__mingw_oldexcpt_handler",   (void *)&mingw_excpt_handler_stub,    0x090 },
    { "__native_startup_lock",      (void *)&native_startup_lock,         0x0a0 },
    { "__native_startup_state",     (void *)&native_startup_state,        0x0b0 },
    { "__xc_a",                     (void *)&xc_a_stub,                   0x0c0 },
    { "__xc_z",                     (void *)&xc_z_stub,                   0x0d0 },
    { "__xi_a (dup)",               (void *)&xi_a_stub,                   0x0e0 },
    { "__xi_z",                     (void *)&xi_z_stub,                   0x0f0 },
    { "_commode",                   (void *)&_commode,                    0x100 },
    { "_dowildcard",                (void *)&dowildcard_val,              0x110 },
    { "_fmode",                     (void *)&_fmode,                      0x120 },
    { "_newmode",                   (void *)&newmode_val,                 0x150 },
    { "mingw_app_type",             (void *)&__msvcrt_app_type,           0x160 },
    { NULL, NULL, 0 }  /* terminator */
};
#define REF_MAP_COUNT (sizeof(refptr_mappings) / sizeof(refptr_mappings[0]) - 1)

/* Helper: apply a single refptr patch with mprotect */
static void apply_refptr_patch(void *image_base, uint64_t rva, void *target,
                               const char *name, uint64_t image_size)
{
    if (rva >= image_size) {
        fprintf(stderr, "patch_crt_refptrs: %s rva 0x%lx >= image_size 0x%lx, skip\n",
                name, (unsigned long)rva, (unsigned long)image_size);
        return;
    }

    uint64_t *refptr = (uint64_t *)((char *)image_base + rva);
    void *old_val = (void *)*refptr;

    /* Make the containing page writable */
    char *page_start = (char *)((uint64_t)(char *)refptr & ~(uint64_t)4095);
    if (mprotect(page_start, 4096, PROT_READ | PROT_WRITE) != 0) {
        perror("patch_crt_refptrs: mprotect");
        return;
    }

    *refptr = (uint64_t)(uintptr_t)target;
    fprintf(stderr, "patch_crt_refptrs: %s at rva 0x%lx: 0x%lx -> %p (stub)\n",
            name, (unsigned long)rva, (unsigned long)old_val, target);

    /* Restore read-only */
    mprotect(page_start, 4096, PROT_READ);
}

void patch_crt_refptrs(void *image_base, IMAGE_NT_HEADERS64 *nt, IMAGE_SECTION_HEADER *sections)
{
    if (!image_base || !nt || !sections) return;

    /* Set g_image_base_ref to actual image base before applying patches */
    g_image_base_ref = (uint64_t)(uintptr_t)image_base;

    /* Dynamically find .bss section to set __imp___initenv_stub and g_bss_vaddr */
    IMAGE_SECTION_HEADER *bss_sec = find_section_by_name(nt, sections, ".bss");
    if (bss_sec) {
        g_bss_vaddr = bss_sec->VirtualAddress;
        /* Set __imp___initenv_stub to point to PE's envp in .bss
         * (the PE writes envp through this pointer) */
        __imp___initenv_stub = (void **)((char *)image_base + g_bss_vaddr + 0x018);
    } else {
        fprintf(stderr, "patch_crt_refptrs: WARNING: .bss section not found\n");
        g_bss_vaddr = 0;
    }

    /* __acrt_iob_func patching is done dynamically in the child process
     * (main.c:jump_to_entry) via find_text_thunk(). No need to patch here. */

    uint64_t image_size = nt->OptionalHeader.SizeOfImage;

    /* Find .refptr section dynamically */
    IMAGE_SECTION_HEADER *refptr_sec = find_section_by_name(nt, sections, ".refptr");
    if (!refptr_sec) {
        fprintf(stderr, "patch_crt_refptrs: WARNING: .refptr section not found, CRT refptr patching disabled\n");
        return;
    }

    uint64_t refptr_base = refptr_sec->VirtualAddress;
    uint64_t refptr_size = refptr_sec->Misc.VirtualSize;
    if (refptr_size == 0)
        refptr_size = refptr_sec->SizeOfRawData;

    fprintf(stderr, "patch_crt_refptrs: .refptr section at VA=0x%lx, size=0x%lx\n",
            (unsigned long)refptr_base, (unsigned long)refptr_size);

    /* Try COFF symbol table for dynamic name-based discovery */
    IMAGE_SYMBOL *symbols = NULL;
    char *string_table = NULL;
    int sym_count = parse_symbol_table_from_image(
        image_base, nt, nt->OptionalHeader.SizeOfHeaders,
        &symbols, &string_table);

    int used_symbol_table = 0;

    for (size_t i = 0; i < REF_MAP_COUNT; i++) {
        const refptr_mapping_t *map = &refptr_mappings[i];
        uint64_t target_rva;

        if (sym_count > 0 && symbols && string_table) {
            /* Try symbol table lookup first.
             * .refptr symbols appear as .refptr.<name> in the COFF table,
             * but the symbol name in the table is just <name> with
             * SectionNumber pointing to .refptr and Value being the
             * offset within the section. */
            uint32_t sym_value = lookup_symbol_value(symbols, sym_count,
                                                      string_table, map->name);
            if (sym_value != 0) {
                target_rva = refptr_base + sym_value;
                if (!used_symbol_table) {
                    fprintf(stderr, "patch_crt_refptrs: using COFF symbol table for discovery\n");
                    used_symbol_table = 1;
                }
            } else {
                /* Symbol not found in table — fall back to relative offset */
                target_rva = refptr_base + map->rel_offset;
            }
        } else {
            /* No symbol table — use fallback relative offsets */
            target_rva = refptr_base + map->rel_offset;
        }

        /* Validate the computed RVA is within the .refptr section */
        if (target_rva < refptr_base || target_rva + 8 > refptr_base + refptr_size) {
            fprintf(stderr, "patch_crt_refptrs: %s computed rva 0x%lx outside .refptr section, skipping\n",
                    map->name, (unsigned long)target_rva);
            continue;
        }

        apply_refptr_patch(image_base, target_rva, map->target,
                           map->name, image_size);
    }
}
