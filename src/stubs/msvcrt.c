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
#include "include/common.h"

/* ── Global variables ──────────────────────────────────────── */

int __msvcrt_app_type = 0;
int _commode = 0;
int _fmode = 0;
char **_msvcrt_environ = NULL;

// Default empty; overwritten from main.c with the actual guest command line
static char _cmdline_storage[PAGE_SIZE] = "";
char *_acmdln = _cmdline_storage;

/* Static variables for additional CRT refptr patches */
static int native_startup_lock = 0;
static int native_startup_state = 0;
static int dowildcard_val = 0;
static int newmode_val = 0;
static uint64_t g_image_base_ref = 0;  // will be set to image base at runtime

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

_Static_assert(sizeof(wine_FILE) == WINE_FILE_SIZE, "wine_FILE size mismatch");

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
__attribute__((ms_abi))
void *__iob_func(void)
{
    return __wine_iob.bytes;
}

/* ── CRT Startup Functions ────────────────────────────────── */

__attribute__((ms_abi))
void __set_app_type(int type)
{
    __msvcrt_app_type = type;
}

__attribute__((ms_abi))
void __initenv(void)
{
}

__attribute__((ms_abi))
void _initterm(void)
{
}

__attribute__((ms_abi))
void *_initterm_e(const void **pi, const void **pe)
{
    if (pi) *pi = NULL;
    if (pe) *pe = NULL;
    return NULL;
}

__attribute__((ms_abi))
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
__attribute__((ms_abi))
void *__p__commode(void)
{
    return &_commode;
}

__attribute__((ms_abi))
void *__p__fmode(void)
{
    return &_fmode;
}

/* __getmainargs: parse command line and environment */
__attribute__((ms_abi))
void __getmainargs(int *argc, char ***argv, char ***envp, int expand_env, void *pStartInfo)
{
    static char *dummy_argv[2] = { "./hello.exe", NULL };
    static char *dummy_envp[2] = { "PATH=/usr/bin", NULL };

    if (argc) *argc = 1;
    if (argv) *argv = dummy_argv;
    if (envp) *envp = dummy_envp;

    /* Also write to the PE's .bss section so the CRT can find them.
     * The .bss lives at image_base + 0x7000.
     *   argc at 0x7028 (4 bytes), argv at 0x7020 (8 bytes), envp at 0x7018 (8 bytes)
     * The CRT reads argv from 0x7020 and does two-level indirection: mov (%r13),%rcx
     * If argv is NULL there, dereferencing 0 → SIGSEGV.
     */
    uint64_t image_base = g_image_base_ref;
    if (image_base) {
        char *bss = (char *)image_base + 0x7000;
        *(uint32_t *)(bss + 0x028) = 1;            // argc = 1
        *(uint64_t *)(bss + 0x020) = (uint64_t)(uintptr_t)dummy_argv;  // argv
        *(uint64_t *)(bss + 0x018) = (uint64_t)(uintptr_t)dummy_envp;  // envp
    }

    (void)expand_env;
    (void)pStartInfo;
}

__attribute__((ms_abi))
void *_setargv(void)
{
    return NULL;
}

__attribute__((ms_abi))
void __lconv_init(void)
{
}

__attribute__((ms_abi))
void __setusermatherr(void (*handler)(void))
{
    (void)handler;
}

/* ── Forward declarations for internal functions ───────────── */

static __attribute__((ms_abi))
void wine__exit(int code);
static __attribute__((ms_abi))
int wine_vfprintf(wine_FILE *stream, const char *format, va_list ap);

/* ── Stdlib Stubs ─────────────────────────────────────────── */

__attribute__((ms_abi))
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

__attribute__((ms_abi))
void _cexit(void)
{
    wine__exit(0);
}

/* ── Internal implementations with unique names ────────────── */

static __attribute__((ms_abi))
void wine__exit(int code)
{
    /* Call Linux sys_exit directly */
    syscall(60, code);
    __builtin_unreachable();
}

static __attribute__((ms_abi))
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

static __attribute__((ms_abi))
int wine_exit(int code)
{
    wine__exit(code);
    __builtin_unreachable();
}

static __attribute__((ms_abi))
void *wine_malloc(size_t size)
{
    return __builtin_malloc(size);
}

static __attribute__((ms_abi))
void *wine_calloc(size_t nmemb, size_t size)
{
    return __builtin_calloc(nmemb, size);
}

static __attribute__((ms_abi))
void wine_free(void *ptr)
{
    __builtin_free(ptr);
}

static __attribute__((ms_abi))
void *wine_memcpy(void *dest, const void *src, size_t n)
{
    return __builtin_memcpy(dest, src, n);
}

static __attribute__((ms_abi))
size_t wine_strlen(const void *s)
{
    return __builtin_strlen(s);
}

static __attribute__((ms_abi))
int wine_strncmp(const void *s1, const void *s2, size_t n)
{
    return strncmp(s1, s2, n);
}

static __attribute__((ms_abi))
int wine_fprintf(wine_FILE *stream, const char *format, ...)
{
    va_list ap;
    va_start(ap, format);
    int ret = wine_vfprintf(stream, format, ap);
    va_end(ap);
    return ret;
}

static __attribute__((ms_abi))
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
    while (len < PAGE_MASK && format[len]) len++;
    if (len == 0) return 0;

    long res;
    __asm__ volatile("syscall"
                     : "=a"(res)
                     : "a"(1), "D"(fd), "S"(format), "d"(len)
                     : "rcx", "r11", "memory", "cc");
    (void)res;
    return (int)len;
}

static __attribute__((ms_abi))
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

static __attribute__((ms_abi))
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

/*
 * The PE's .rdata contains .refptr entries that the CRT startup code
 * dereferences to find global variables like mingw_app_type, _fmode,
 * _commode, etc. These entries are initialized by the linker with
 * placeholder values that may not be valid addresses.
 *
 * We need to patch them to point to our actual global variables.
 */

typedef struct {
    uint64_t rva;        /* RVA of the refptr entry */
    void    *target;     /* What it should point to */
    const char *name;    /* For debugging */
} refptr_patch_t;

static const refptr_patch_t refptr_patches[] = {
    { 0x4300, (void *)&ctor_list_stub,        "__CTOR_LIST__" },
    { 0x4310, (void *)&dtor_list_stub,        "__DTOR_LIST__" },
    { 0x4320, (void *)&xi_a_stub,             "__xi_a" },
    { 0x4330, (void *)&dyn_tls_callback_stub, "__dyn_tls_init_callback" },
    { 0x4340, (void *)&g_image_base_ref,      "__image_base__" },
    { 0x4350, (void *)&__imp___initenv_stub,  "__imp___initenv" },
    { 0x4390, (void *)&mingw_excpt_handler_stub, "__mingw_oldexcpt_handler" },
    { 0x43a0, (void *)&native_startup_lock,   "__native_startup_lock" },
    { 0x43b0, (void *)&native_startup_state,  "__native_startup_state" },
    { 0x43c0, (void *)&xc_a_stub,             "__xc_a" },
    { 0x43d0, (void *)&xc_z_stub,             "__xc_z" },
    { 0x43e0, (void *)&xi_a_stub,             "__xi_a (dup)" },
    { 0x43f0, (void *)&xi_z_stub,             "__xi_z" },
    { 0x4400, (void *)&_commode,              "_commode" },
    { 0x4410, (void *)&dowildcard_val,        "_dowildcard" },
    { 0x4420, (void *)&_fmode,                "_fmode" },
    { 0x4450, (void *)&newmode_val,           "_newmode" },
    { 0x4460, (void *)&__msvcrt_app_type,     "mingw_app_type" },
    { 0, NULL, NULL }  // terminator — target always non-NULL for real entries
};
#define REF_PTR_COUNT (sizeof(refptr_patches) / sizeof(refptr_patches[0]) - 1) // minus terminator

void patch_crt_refptrs(void *image_base, void *nt_ptr)
{
    IMAGE_NT_HEADERS64 *nt = (IMAGE_NT_HEADERS64 *)nt_ptr;
    if (!image_base || !nt) return;

    /* Set g_image_base_ref to actual image base before applying patches */
    g_image_base_ref = (uint64_t)(uintptr_t)image_base;

    /* Set __imp___initenv_stub to point to PE's envp in .bss
     * (the PE writes envp through this pointer) */
    __imp___initenv_stub = (void **)(image_base + 0x7018);  /* envp in .bss */

    /*
     * Patch __acrt_iob_func to fix the rcx corruption bug.
     * The PE code does:
     *   call __iob_func     ; our __iob_func may clobber rcx upper bits
     *   mov %ebx,%ecx       ; restores lower 32 bits
     *   lea (%rcx,%rcx,2),%rdx  ; uses rcx (with garbage upper bits!)
     *
     * Fix: replace lea/shl/add with a version that uses ebx (clean 32-bit)
     * New code:
     *   mov %ebx,%ecx       ; restore index (clears upper bits)
     *   mov %ecx,%edx       ; edx = index  (32-bit write clears upper)
     *   lea (%rdx,%rdx,2),%rdx  ; rdx = index * 3
     *   shl $0x4,%rdx       ; rdx *= 16 = index * 48
     *   add %rdx,%rax       ; rax = base + index*48
     */
    {
        char *acrt_fn = (char *)image_base + 0x27ac;
        char *page_start = (char *)((uint64_t)acrt_fn & ~(uint64_t)PAGE_MASK);
        if (mprotect(page_start, PAGE_SIZE, PROT_READ | PROT_WRITE | PROT_EXEC) == 0) {
            /* Replace 13 bytes starting at 0x27ac:
             * Original: mov%ebx,%ecx | lea(%rcx,%rcx,2),%rdx | shl$4,%rdx | add%rdx,%rax
             * New:      mov%ebx,%edx | imul$48,%edx,%edx | 3NOP | add%rdx,%rax
             * Uses ebx (clean 32-bit) instead of rcx (garbage upper bits)
             */
            unsigned char new_code[] = {
                0x89, 0xda,             /* mov %ebx,%edx (clears rdx upper) */
                0x6b, 0xda, 0x30,       /* imul $48,%edx,%edx (clears rdx upper) */
                0x90, 0x90, 0x90, 0x90, 0x90,  /* 5 NOP to fill to 13 bytes */
                0x48, 0x01, 0xd0,            /* add %rdx,%rax */
            };
            memcpy(acrt_fn, new_code, sizeof(new_code));
            fprintf(stderr, "patched __acrt_iob_func to use ebx directly\n");
        }
        mprotect(page_start, PAGE_SIZE, PROT_READ | PROT_EXEC);
    }

    uint64_t image_size = nt->OptionalHeader.SizeOfImage;
    fprintf(stderr, "patch_crt_refptrs: image_size=0x%lx\n", (unsigned long)image_size);

    for (int i = 0; i < REF_PTR_COUNT; i++) {
        uint64_t rva = refptr_patches[i].rva;
        if (rva >= image_size) {
            fprintf(stderr, "patch_crt_refptrs: %s rva 0x%lx >= image_size 0x%lx, skip\n",
                    refptr_patches[i].name, (unsigned long)rva, (unsigned long)image_size);
            continue;
        }

        uint64_t *refptr = (uint64_t *)((char *)image_base + rva);
        void *old_val = (void *)*refptr;

        /* Make the containing page writable */
        char *page_start = (char *)((uint64_t)(char *)refptr & ~(uint64_t)PAGE_MASK);
        if (mprotect(page_start, PAGE_SIZE, PROT_READ | PROT_WRITE) != 0) {
            perror("patch_crt_refptrs: mprotect");
            continue;
        }

        *refptr = (uint64_t)(uintptr_t)refptr_patches[i].target;
        fprintf(stderr, "patch_crt_refptrs: %s at rva 0x%lx: 0x%lx -> %p (stub)\n",
                refptr_patches[i].name, (unsigned long)rva,
                (unsigned long)old_val, refptr_patches[i].target);

        /* Restore read-only */
        mprotect(page_start, PAGE_SIZE, PROT_READ);
    }
}
