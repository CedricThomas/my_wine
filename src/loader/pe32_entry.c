/*
 * pe32_entry.c — 32-bit ELF entry point for my_wine32
 *
 * This is the C entry point for the 32-bit child process in the
 * dual-process PE32 execution model.
 *
 * The 32-bit child is a STANDALONE process that independently:
 *   1. Reads the PE file path from WINE32_PE_PATH environment variable
 *   2. Opens and maps the PE image at the preferred (or default 0x00400000) base
 *   3. Allocates TEB at fixed address 0x7FFDE000 and PEB at 0x7FFDF000
 *   4. Initializes TEB/PEB (self-references, PEB pointer, image base)
 *   5. Generates 32-bit syscall thunks
 *   6. Sets up POSIX signal handlers
 *   7. Jumps to the PE entry point (or user entry symbol like D_DoomMain)
 *
 * Sets FS→TEB via set_thread_area (syscall 243) because
 * arch_prctl(ARCH_SET_FS) returns EINVAL in 32-bit mode on a 64-bit kernel.
 * Wine uses the same approach: allocate an LDT entry, then load the
 * returned selector into %fs.
 *
 * Uses custom getenv to avoid glibc TLS (which uses GS-relative
 * accesses that may be broken if GS has been switched).
 *
 * Compiled with -m32, uses int $0x80 syscalls via syscalls_inline.h.
 * Called from glibc CRT (__libc_start_main → main).
 */

#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>

#include "include/crt.h"
#include "../syscall/syscalls_inline.h"
#include "include/syscall/dispatcher_entry.h"
#include "include/pe.h"
#include "include/pe_priv.h"
#include "include/pe_parser.h"
#include "include/nt_constants.h"
#include "include/common.h"
#include "include/syscall/thunk_gen.h"
#include "teb_peb.h"
#include "import_init.h"
#include "import_table.h"
#include "import_resolve.h"
#include "peb_ldr.h"
#include "module_list.h"
#include "loader_utils.h"
#include "../heap/wine_heap.h"

/*
 * Local BSS offset defines for MinGW CRT layout.
 *
 * This is a standalone 32-bit binary that cannot load CRT modules,
 * so we use hardcoded offsets matching the MinGW CRT .bss layout.
 */
#define CRT_BSS_INITENV   0x018   /* __initenv / _environ pointer */
#define CRT_BSS_ARGV      0x020   /* _argv pointer */
#define CRT_BSS_ARGC      0x028   /* _argc */
#define CRT_BSS_ACMDLN    0x030   /* _acmdln pointer (for GetCommandLineA) */

/*
 * PE32 threading safety
 *
 * The handle manager (src/msvcrt/handle_manager.c) uses spinlocks
 * (wine_spinlock_t) instead of pthread mutexes. This avoids glibc's
 * GS-relative TLS accesses which would break after the FS→TEB switch.
 * Similarly, wine_heap.c gates all pthread_mutex_* behind #ifndef MY_WINE32.
 * The 64-bit build (src/msvcrt/ntdll_*.c) uses the same spinlock approach.
 *
 * glibc uses GS for TLS on i386 — no conflict with FS→TEB.
 */

/* Declarations from image_mapper.c (linked into my_wine32) */

/* Module-level vars for NT headers (needed for setup_stack and other modules) */
static IMAGE_NT_HEADERS g_nt_headers;
void *map_image(const char *path, IMAGE_DOS_HEADER *out_dos,
                IMAGE_NT_HEADERS *out_nt, size_t *out_nt_size);

/* Declarations from crash_handlers.c (linked into my_wine32) */
extern void setup_signal_handlers(void);
extern void seh_crash_handler(void *, void *, void *, void *);

/* Declaration from pe32_run_guest.S */

/* _acmdln — command-line string buffer from crt_32_stub.c (256 bytes).
 * Seeded in main() so GetCommandLineA() returns the PE path. */
extern char _acmdln[];
extern void pe32_run_guest(uint32_t entry_abs, void *stack_top) __attribute__((noreturn));

/*
 * 32-bit argv/envp setup for CRT bypass.
 * g_argv_ptr and g_argv_page store the allocated 32-bit argv array and
 * its backing page (allocated with MAP_32BIT below 4GB). These are set
 * up by ensure_argv_setup() and used by both seed_bss_vars() and
 * setup_fs_and_jump() to provide consistent CRT globals.
 */
static uint32_t g_argv_ptr = 0;  /* 32-bit address of the argv array */
void *g_argv_page = NULL; /* backing page (MAP_32BIT) */

/**
 * ensure_argv_setup — allocate 32-bit argv/envp arrays and path copy.
 *
 * Allocates a page below 4GB (MAP_32BIT), copies pe_path into it,
 * then builds:
 *   argv = { path_copy, NULL }
 *   envp = { (char*)environ }
 *
 * Stores result in g_argv_ptr (for BSS seeding) and g_argv_page.
 * Idempotent: second call is a no-op if g_argv_ptr is already set.
 */
static void ensure_argv_setup(const char *pe_path)
{
    if (g_argv_ptr != 0)
        return;  /* already set up */

    /* Allocate page below 4GB for argv/envp arrays and path copy */
    void *page = INLINE_SYSCALL_MMAP(NULL, PAGE_SIZE,
        PROT_READ | PROT_WRITE,
        MAP_PRIVATE | MAP_ANONYMOUS | MAP_32BIT, -1, 0);
    if (page == MAP_FAILED || page == NULL) {
        const char err[] = "my_wine32: failed to alloc 32-bit argv page\n";
        INLINE_SYSCALL_WRITE_ERR(err, sizeof(err) - 1);
        INLINE_SYSCALL_EXIT_GROUP(1);
    }
    g_argv_page = page;

    uint8_t *p = (uint8_t *)page;
    memset(page, 0, PAGE_SIZE);

    /* Copy pe_path into the 32-bit page (argv[0] string).
     * Use dll_copy_str instead of strncpy: in the 32-bit standalone build,
     * musl's strncpy is an ifunc whose PLT resolver returns without executing
     * the actual copy (compiler ifunc bug). dll_copy_str is a static inline
     * that uses only __builtin_ operations, avoiding the ifunc issue. */
    char *path_copy = (char *)(p + 0);       /* offset 0x00 */
    dll_copy_str(path_copy, pe_path, 511);
    path_copy[510] = '\0';

    /* Build argv array at offset 0x200 (512 bytes into the page) */
    uint32_t *argv = (uint32_t *)(p + 0x200);
    argv[0] = (uint32_t)(uintptr_t)path_copy;
    argv[1] = 0;  /* NULL terminator */

    /* Build envp array at offset 0x208 */
    uint32_t *envp = (uint32_t *)(p + 0x208);
    {
        extern char **environ;
        envp[0] = (uint32_t)(uintptr_t)environ;
    }
    envp[1] = 0;  /* NULL terminator */

    g_argv_ptr = (uint32_t)(uintptr_t)argv;
}

/* ── Error messages (null-terminated, written via syscall to stderr) ── */
static const char err_bad_env[]    = "my_wine32: missing or invalid WINE32_PE_PATH\n";
static const char err_map[]        = "my_wine32: failed to map PE image\n";
static const char err_teb[]        = "my_wine32: failed to allocate TEB\n";
static const char err_peb[]        = "my_wine32: failed to allocate PEB\n";
static const char err_unix_stack[] = "my_wine32: failed to setup UNIX stack\n";
static const char err_thunks[]     = "my_wine32: failed to generate thunks\n";
static const char err_stack[]      = "my_wine32: failed to setup guest stack\n";
static const char err_fs[]         = "my_wine32: set_thread_area (FS→TEB) failed\n";
static const char err_import[]     = "my_wine32: import resolution failed\n";

/* ── Externs for PEB wiring ──────────────────────────────────── */
extern void *g_process_heap;

/* ── SEH frame (stable location for TEB+0x00 exception chain) ──
 * EXCEPTION_REGISTRATION_RECORD: placed in static BSS so the TEB
 * SEH pointer (TEB+0x00) always has a valid chain entry.
 * Next = 0xFFFFFFFF terminates the chain (no parent handler).
 * Handler = seh_crash_handler dumps diagnostics and exits. */
struct exception_registration_record {
    uint32_t next;
    uint32_t handler;
};
static struct exception_registration_record g_seh_frame = {
    .next    = 0xFFFFFFFF,
    .handler = 0,  /* filled in init_teb32 */
};

/* ── Custom getenv — no glibc TLS dependency ──────────────────── */

/* String helpers from crt_32_stub.c (linked into my_wine32) */
extern size_t _m_strnlen(const char *s, size_t n);
extern int _m_strncmp(const char *a, const char *b, size_t n);

/*
 * my_getenv — look up an environment variable by scanning the
 * global environ array directly. Does not depend on glibc TLS
 * (which uses GS-relative accesses that may be broken).
 */
static const char *my_getenv(const char *key)
{
    extern char **environ;
    size_t klen = _m_strnlen(key, 4096);

    for (int i = 0; environ[i]; i++) {
        if (_m_strncmp(environ[i], key, klen) == 0 && environ[i][klen] == '=') {
            return environ[i] + klen + 1;
        }
    }
    return NULL;
}

/* ── Helpers ──────────────────────────────────────────────────── */

/*
 * map_pe — opens and maps the PE image.
 * Returns the mapped base address or NULL on failure.
 */
static void *map_pe(const char *path)
{
    IMAGE_DOS_HEADER dos;
    IMAGE_NT_HEADERS nt;
    size_t nt_size;

    void *base = map_image(path, &dos, &nt, &nt_size);
    if (!base) {
        INLINE_SYSCALL_WRITE_ERR(err_map, sizeof(err_map) - 1);
        INLINE_SYSCALL_EXIT_GROUP(1);
    }

    /* Verify we got a PE32 image */
    if (!pe_is_pe32(&nt)) {
        const char err_type[] = "my_wine32: not a PE32 image\n";
        INLINE_SYSCALL_WRITE_ERR(err_type, sizeof(err_type) - 1);
        INLINE_SYSCALL_EXIT_GROUP(1);
    }

    /* Save NT headers for setup_stack and other modules */
    memcpy(&g_nt_headers, &nt, sizeof(g_nt_headers));

    /* Mark as 32-bit build so teb_peb.c/heap use 32-bit paths */
    g_loader.is_32bit = 1;

    return base;
}

#ifndef AT_FDCWD
#define AT_FDCWD ((long)-100)
#endif

/*
 * resolve_entry — returns the PE entry point RVA.
 *
 * For PE32: tries to find the _main symbol in the COFF symbol table
 * so we can bypass the MinGW CRT startup (___tmainCRTStartup) and
 * jump directly to the user's main(). This avoids the CRT relocator,
 * exception filter setup, FPU reset, and other CRT init code that
 * causes crashes in the 32-bit loader (EIP=0x0 after _out returns).
 *
 * When jumping to _main, we also set the _initialized CRT flag so that
 * _main's call to ___main returns immediately without running
 * __do_global_ctors (which can crash with inconsistent CRT state).
 *
 * If _main is not found, falls back to the PE AddressOfEntryPoint.
 */
static uint32_t resolve_entry(const char *path)
{
    uint32_t entry_rva = pe_entry_rva(&g_nt_headers);
    uint32_t ptr_sym = pe_pointer_to_symbol_table(&g_nt_headers);
    uint32_t num_sym = pe_number_of_symbols(&g_nt_headers);

    if (ptr_sym == 0 || num_sym == 0)
        return entry_rva;

    IMAGE_SYMBOL *symbols = NULL;
    char *string_table = NULL;
    int sym_count = parse_symbol_table_from_file(path, &g_nt_headers,
                                                  &symbols, &string_table);
    if (sym_count <= 0)
        return entry_rva;

    IMAGE_SECTION_HEADER *sections =
        get_image_sections(g_loader.image_base, &g_nt_headers);
    int num_sections = pe_section_count(&g_nt_headers);

    /* Try _main first (MinGW convention for user's main) */
    uint32_t main_rva = lookup_symbol_rva(symbols, sym_count, string_table,
                                           sections, num_sections, "_main");
    if (main_rva == 0) {
        /* Try main without underscore */
        main_rva = lookup_symbol_rva(symbols, sym_count, string_table,
                                      sections, num_sections, "main");
    }

    if (main_rva != 0) {
        entry_rva = main_rva;

        /* Set the _initialized CRT flag so ___main skips __do_global_ctors. */
        uint32_t init_rva = lookup_symbol_rva(symbols, sym_count, string_table,
                                               sections, num_sections, "_initialized");
        if (init_rva == 0) {
            IMAGE_SECTION_HEADER *bss = find_section_by_name(&g_nt_headers,
                                                             sections, ".bss");
            if (bss)
                init_rva = bss->VirtualAddress + 0x40;
        }
        if (init_rva != 0) {
            uint8_t *addr = pe_rva_to_ptr(g_loader.image_base, &g_nt_headers,
                                          init_rva, sizeof(uint32_t));
            if (addr == NULL) {
                free(symbols);
                return entry_rva;
            }
            uintptr_t page = (uintptr_t)addr & ~(uintptr_t)PAGE_MASK;
            long rc = INLINE_SYSCALL_MPROTECT((void *)page, PAGE_SIZE,
                                               PROT_READ | PROT_WRITE);
            if (rc == 0)
                *(uint32_t *)addr = 1;
        }
    }

    free(symbols);
    return entry_rva;
}

/*
 * init_teb32 — allocates TEB at 0x7FFDE000, sets self-ref,
 * thread ptr, PEB pointer, and GDI offsets.
 */
static void *init_teb32(void *peb)
{
    void *teb = INLINE_SYSCALL_MMAP(
        (void *)(uintptr_t)TEB32_FIXED_ADDR,
        PAGE_SIZE,
        PROT_READ | PROT_WRITE,
        MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED,
        -1, 0);
    if (teb == MAP_FAILED) {
        INLINE_SYSCALL_WRITE_ERR(err_teb, sizeof(err_teb) - 1);
        INLINE_SYSCALL_EXIT_GROUP(1);
    }

    memset(teb, 0, PAGE_SIZE);

    init_teb32_fields(teb, peb);

    /* Wire SEH chain at TEB+0x00 → g_seh_frame. */
    g_seh_frame.handler = (uint32_t)(uintptr_t)seh_crash_handler;
    *(uint32_t *)((uint8_t *)teb + TEB32_SEH_CHAIN) =
        (uint32_t)(uintptr_t)&g_seh_frame;

    /* Wire EnvironmentPointer at TEB+0x48 (gap after ClientId) to environ. */
    {
        extern char **environ;
        *(uint32_t *)((uint8_t *)teb + 0x48) =
            (uint32_t)(uintptr_t)environ;
    }

    /* Wire ClientId (TEB+0x40/0x44) = getpid()/gettid(). */
    *(uint32_t *)((uint8_t *)teb + 0x40) = (uint32_t)INLINE_SYSCALL_GETPID();
    *(uint32_t *)((uint8_t *)teb + 0x44) = (uint32_t)INLINE_SYSCALL_GETTID();

    return teb;
}

/*
 * init_peb32 — allocates PEB at 0x7FFDF000, sets image base and
 * BeingDebugged.
 */
static void *init_peb32(void *base)
{
    void *peb = INLINE_SYSCALL_MMAP(
        (void *)(uintptr_t)PEB32_FIXED_ADDR,
        PAGE_SIZE,
        PROT_READ | PROT_WRITE,
        MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED,
        -1, 0);
    if (peb == MAP_FAILED) {
        INLINE_SYSCALL_WRITE_ERR(err_peb, sizeof(err_peb) - 1);
        INLINE_SYSCALL_EXIT_GROUP(1);
    }

    memset(peb, 0, PAGE_SIZE);

    init_peb32_fields(peb, base);

    return peb;
}

/*
 * prepare_dispatch — generates syscall thunks and installs signal handlers.
 */
static void prepare_dispatch(void)
{
    if (setup_unix_stack() != 0) {
        INLINE_SYSCALL_WRITE_ERR(err_unix_stack, sizeof(err_unix_stack) - 1);
        INLINE_SYSCALL_EXIT_GROUP(1);
    }

    void **thunk_arr = generate_all_thunks();
    if (!thunk_arr) {
        INLINE_SYSCALL_WRITE_ERR(err_thunks, sizeof(err_thunks) - 1);
        INLINE_SYSCALL_EXIT_GROUP(1);
    }
    (void)thunk_arr;

    setup_signal_handlers();
}

/* ── PEB field wiring (split into sub-functions) ──────────────────── */

/**
 * wire_peb32_heap — initialize process heap and write into PEB.
 */
static void wire_peb32_heap(void *peb)
{
    uint8_t *p = (uint8_t *)peb;

    if (g_process_heap == NULL) {
        g_process_heap = init_process_heap();
    }
    if (g_process_heap) {
        uint32_t heap_val = (uint32_t)(uintptr_t)g_process_heap;
        *(uint32_t *)(p + 0x18) = heap_val;   /* WinXP PEB.ProcessHeap */
        *(uint32_t *)(p + 0x3C) = heap_val;   /* Win7+ PEB.ProcessHeap */
    }
}

/**
 * wire_peb32_params — create RTL_USER_PROCESS_PARAMETERS.
 *
 * Standard Win32 (32-bit) layout:
 *   0x00  MaximumLength (ULONG)
 *   0x04  Length (ULONG)
 *   0x08  Reserved1 (ULONG)
 *   0x0C  Reserved2 (ULONG)
 *   0x10  CurrentDirectoryHandle (PVOID)
 *   0x14  CurrentDirectoryDosPathPointer (PVOID → UNICODE_STRING)
 *   0x18  DllPathPointer (PVOID → UNICODE_STRING)
 *   0x1C  CommandLinePointer (PVOID → UNICODE_STRING)
 *
 * All unicode strings are stored in the mmap'd page after the struct
 * fields to ensure they persist beyond the calling stack frame.
 */
static void wire_peb32_params(void *peb, const char *pe_path)
{
    uint8_t *p = (uint8_t *)peb;

    /* Allocate a page for the params struct, below 4GB */
    void *params = INLINE_SYSCALL_MMAP(NULL, PAGE_SIZE,
        PROT_READ | PROT_WRITE,
        MAP_PRIVATE | MAP_ANONYMOUS | MAP_32BIT, -1, 0);
    if (params == MAP_FAILED || params == NULL) {
        return;
    }

    uint8_t *q = (uint8_t *)params;
    memset(params, 0, PAGE_SIZE);

    /* MaximumLength = 0x200 (512 byte allocation) */
    *(uint32_t *)(q + 0x00) = 0x200;
    /* Length = actual used portion of the struct (through CommandLine string) */
    *(uint32_t *)(q + 0x04) = 0x0E8;
    /* Reserved1 = 0 (already zeroed) */
    /* Reserved2 = 0 (already zeroed) */
    /* CurrentDirectoryHandle = NULL (already zeroed) */

    /* ── CurrentDirectoryDosPath: L"C:\\" ──
     * Pointer at 0x14 → UNICODE_STRING at 0x50
     * String data at 0x60 in the mmap'd page. */
    {
        uint16_t *cur_str = (uint16_t *)(q + 0x60);
        cur_str[0] = 'C'; cur_str[1] = ':'; cur_str[2] = '\\'; cur_str[3] = 0;

        /* UNICODE_STRING at 0x50 */
        *(uint16_t *)(q + 0x50) = 6;                          /* Length = 3 chars * 2 */
        *(uint16_t *)(q + 0x52) = 8;                          /* MaximumLength */
        *(uint32_t *)(q + 0x54) = (uint32_t)(uintptr_t)cur_str; /* Buffer */

        /* Pointer at PEB field */
        *(uint32_t *)(q + 0x14) = (uint32_t)(uintptr_t)(q + 0x50);
    }

    /* ── DllPath: L"C:\\" (shares string data with CurrentDirectory) ──
     * Pointer at 0x18 → UNICODE_STRING at 0x68
     * String data at 0x60 in the mmap'd page (shared with CurrentDirectory).
     *
     * The UNICODE_STRING is placed at 0x68 (after the 6-byte string at 0x60-0x65)
     * so its Buffer field at 0x6C does NOT overlap the string data. */
    {
        uint16_t *dll_str = (uint16_t *)(q + 0x60);  /* same as cur_str */

        /* UNICODE_STRING at 0x68 */
        *(uint16_t *)(q + 0x68) = 6;                          /* Length */
        *(uint16_t *)(q + 0x6A) = 8;                          /* MaximumLength */
        *(uint32_t *)(q + 0x6C) = (uint32_t)(uintptr_t)dll_str; /* Buffer */

        /* Pointer at PEB field */
        *(uint32_t *)(q + 0x18) = (uint32_t)(uintptr_t)(q + 0x68);
    }

    /* ── CommandLine: pe_path as UTF-16LE ──
     * Pointer at 0x1C → UNICODE_STRING at 0x74
     * String data at 0x80 in the mmap'd page. */
    {
        uint16_t *cmd_buf = (uint16_t *)(q + 0x80);
        size_t cmd_len = 0;
        const char *s = pe_path;
        while (*s && cmd_len < 255) {
            cmd_buf[cmd_len] = (uint16_t)(uint8_t)*s;
            s++;
            cmd_len++;
        }
        cmd_buf[cmd_len] = 0;

        /* UNICODE_STRING at 0x74 */
        *(uint16_t *)(q + 0x74) = (uint16_t)(cmd_len * 2);      /* Length */
        *(uint16_t *)(q + 0x76) = (uint16_t)((cmd_len + 1) * 2); /* MaximumLength */
        *(uint32_t *)(q + 0x78) = (uint32_t)(uintptr_t)cmd_buf; /* Buffer */

        /* Pointer at PEB field */
        *(uint32_t *)(q + 0x1C) = (uint32_t)(uintptr_t)(q + 0x74);
    }

    /* Set PEB.ProcessParameters pointer */
    *(uint32_t *)(p + 0x10) = (uint32_t)(uintptr_t)params;
}

/**
 * wire_peb32_ldr — initialize PEB_LDR_DATA and register main PE image.
 */
static void wire_peb32_ldr(void *peb, void *image_base, IMAGE_NT_HEADERS *nt)
{
    uint8_t *p = (uint8_t *)peb;

    if (loader_get_peb_ldr() == NULL) {
        init_module_list();
        loader_set_peb_ldr(init_peb_ldr());
    }
    if (loader_get_peb_ldr()) {
        *(uint32_t *)(p + 0x0C) = (uint32_t)(uintptr_t)loader_get_peb_ldr();

        /* Register main PE image if not already linked */
        int mod_idx = -1;
        for (int i = 0; i < g_loader.module_count && i < MAX_MODULES; i++) {
            if (g_loader.modules[i].base == image_base) {
                mod_idx = i;
                break;
            }
        }
        if (mod_idx < 0) {
            loaded_module_t *mod = add_module(image_base, "main.exe", nt);
            if (mod) {
                ldr_add_module(mod);
            }
        } else if (!g_loader.modules[mod_idx].ldr_linked) {
            ldr_add_module(&g_loader.modules[mod_idx]);
        }
    }
}

/**
 * wire_peb32_os_version — set OSMajorVersion, OSMinorVersion, OSBuildNumber.
 * Windows 10 21H2: 10.0.19041
 */
static void wire_peb32_os_version(void *peb)
{
    uint8_t *p = (uint8_t *)peb;

    *(uint16_t *)(p + 0x2E) = 0x0A;     /* OSMajorVersion = 10 */
    *(uint16_t *)(p + 0x30) = 0x00;     /* OSMinorVersion = 0 */
    *(uint16_t *)(p + 0x34) = 0x4A11;   /* OSBuildNumber = 19041 */
}

/**
 * wire_peb32_fields — initialize all PEB fields beyond the basic ImageBase.
 * Delegates to sub-functions for each PEB region.
 */
static void wire_peb32_fields(void *peb, void *image_base,
                              IMAGE_NT_HEADERS *nt, const char *pe_path)
{
    wire_peb32_heap(peb);
    wire_peb32_params(peb, pe_path);
    wire_peb32_ldr(peb, image_base, nt);
    wire_peb32_os_version(peb);
}

/* ── BSS Seeding ──────────────────────────────────────────────── */

/*
 * seed_bss_vars — pre-seed CRT globals in the PE .bss section.
 *
 * Many PE32 CRTs (MinGW, Watcom) expect _argc, _argv, _environ
 * to be pre-initialized in .bss before entry. We use local
 * CRT_BSS_* defines for the 32-bit standalone build since the
 * CRT module system is not available in the 32-bit child.
 *
 * Uses INLINE_SYSCALL_MPROTECT for mprotect (already in syscalls_inline.h).
 */
static void seed_bss_vars(void *base, IMAGE_NT_HEADERS *nt)
{
    IMAGE_SECTION_HEADER *sections = get_image_sections(base, nt);
    IMAGE_SECTION_HEADER *bss_sec = find_section_by_name(nt, sections, ".bss");
    if (bss_sec == NULL) {
        return;  /* No .bss — nothing to seed */
    }

    size_t bss_size = bss_sec->Misc.VirtualSize;
    if (bss_size == 0) bss_size = bss_sec->SizeOfRawData;
    if (bss_size == 0) return;

    uint8_t *bss_base = pe_rva_to_ptr(base, nt, bss_sec->VirtualAddress,
                                      bss_size);
    if (bss_base == NULL) return;

    /* Ensure .bss is writable */
    uintptr_t bss_page = (uintptr_t)bss_base & ~(uintptr_t)PAGE_MASK;
    size_t bss_pages = ((bss_size + PAGE_MASK) & ~(size_t)PAGE_MASK);
    if (bss_pages == 0) bss_pages = PAGE_SIZE;
    long mprot_rc = INLINE_SYSCALL_MPROTECT((void *)bss_page, bss_pages,
                                             PROT_READ | PROT_WRITE);
    if (mprot_rc != 0) {
        return;  /* Can't mprotect — skip seeding */
    }

    /* _argc = 1 */
    if (CRT_BSS_ARGC < bss_size) {
        *(uint32_t *)(bss_base + CRT_BSS_ARGC) = 1;
    }

    /* _argv → our 32-bit argv array (set by ensure_argv_setup) */
    if (CRT_BSS_ARGV < bss_size && g_argv_ptr != 0) {
        *(uint32_t *)(bss_base + CRT_BSS_ARGV) = g_argv_ptr;
    }

    /* _envp/__initenv → our 32-bit envp array */
    if (CRT_BSS_INITENV < bss_size && g_argv_page != NULL) {
        uint32_t envp_ptr = (uint32_t)(uintptr_t)((uint8_t *)g_argv_page + 0x208);
        *(uint32_t *)(bss_base + CRT_BSS_INITENV) = envp_ptr;
    }

    /* _acmdln → pointer to the 32-bit path copy (from ensure_argv_setup)
     * so the PE's own CRT _acmdln symbol resolves to a 32-bit string buffer. */
    if (CRT_BSS_ACMDLN + 4 <= bss_size && g_argv_page != NULL) {
        uint32_t path_ptr = (uint32_t)(uintptr_t)((uint8_t *)g_argv_page + 0);
        *(uint32_t *)(bss_base + CRT_BSS_ACMDLN) = path_ptr;
    }
}

/* ── setup_fs_and_jump ──────────────────────────────────────── */

static __attribute__((noreturn)) void setup_fs_and_jump(void *teb,
        const char *pe_path, uint32_t entry_abs, void *stack_top)
{
    /*
     * Ensure 32-bit argv/envp arrays are allocated.
     * This also populates g_argv_ptr used by seed_bss_vars.
     */
    ensure_argv_setup(pe_path);

    /*
     * Zero top of guest stack (first 64 bytes) and write a proper
     * cdecl argument frame: argc=1, argv, envp. This bypasses the
     * MinGW CRT startup (_mainCRTStartup → __getmainargs → main)
     * and jumps directly to _main with correct arguments.
     *
     * Layout on guest stack at ESP alignment:
     *   [ESP+0]  = argc (1)
     *   [ESP+4]  = argv pointer
     *   [ESP+8]  = envp pointer
     *
     * Compute the aligned ESP exactly as pe32_run_guest.S does:
     *   movl stack_top, %esp
     *   andl $-16, %esp
     *   subl $4, %esp
     *   ESP = (stack_top & ~15) - 4
     */
    uintptr_t aligned_sp = ((uintptr_t)stack_top & ~(uintptr_t)15) - 4;
    uint8_t *sp = (uint8_t *)aligned_sp;
    /* Zero the argument frame region (16 bytes: return addr + 3 args).
     * sp = stack_top - 12, so sp + 16 = stack_top + 4.
     * The stack grows downward from stack_top, so the usable region
     * is below stack_top. We zero only the 16 bytes we actually use,
     * staying within the last page of the committed stack. */
    __builtin_memset(sp, 0, 16);

    /*
     * Write a proper cdecl argument frame with fake return address.
     * pe32_run_guest.S does `jmp *entry_abs` (no `call`), so no return
     * address is pushed. We write entry_abs itself as a fake return addr
     * so if main() does `ret`, it re-enters the entry point.
     *
     * Layout at ESP:
     *   [ESP+0]  = fake return address (entry_abs)
     *   [ESP+4]  = argc (1)
     *   [ESP+8]  = argv pointer
     *   [ESP+12] = envp pointer
     */
    *(uint32_t *)(sp + 0) = entry_abs;   /* fake return addr */
    *(uint32_t *)(sp + 4) = 1;           /* argc = 1 */
    *(uint32_t *)(sp + 8) = g_argv_ptr;  /* argv = 32-bit array */
    *(uint32_t *)(sp + 12) = (uint32_t)(uintptr_t)((uint8_t *)g_argv_page + 0x208); /* envp */

    /* Set FS → TEB so guest fs:[offset] accesses resolve to TEB.
     * arch_prctl(ARCH_SET_FS) returns EINVAL in 32-bit mode on a 64-bit kernel.
     * Use set_thread_area (syscall 243) to allocate an LDT entry pointing
     * to the TEB, then load the returned selector into %fs (Wine approach). */
    struct modify_ldt_ldt_s ldt = {
        .entry_number    = -1,           /* auto-allocate */
        .base_addr       = (unsigned int)(uintptr_t)teb,
        .limit           = 0xFFFFF,      /* 1MB, byte granularity */
        .seg_32bit       = 1,            /* 32-bit segment */
        .contents        = 0,            /* data segment */
        .read_exec_only  = 0,            /* read/write */
        .limit_in_pages  = 0,            /* byte granularity */
        .seg_not_present = 0,
        .usable          = 1,
        .garbage         = 0
    };
    long ldt_rc = INLINE_SYSCALL_SET_THREAD_AREA(&ldt);
    if (ldt_rc < 0) {
        INLINE_SYSCALL_WRITE_ERR(err_fs, sizeof(err_fs) - 1);
    } else {
        uint16_t fs_sel = ((uint16_t)ldt.entry_number << 3) | 3;  /* LDT, RPL=3 */
        __asm__ volatile("mov %0, %%fs" : : "r"(fs_sel) : "memory");
    }
    /* Note: even if FS set fails, we continue — some guests may not use fs: */

    /* Switch to guest stack and jump to PE entry point */
    pe32_run_guest(entry_abs, stack_top);
}

/* ── main ─────────────────────────────────────────────────────── */

/**
 * main — C entry point for my_wine32.
 *
 * Orchestrates: read env → map PE → resolve entry → init PEB/TEB →
 * generate thunks → jump to entry.
 *
 * @return never returns (jumps to PE code or exits via syscall)
 */
int main(int argc, char **argv)
{
    const char *pe_path;
    uint32_t entry_rva;
    uint32_t entry_abs;

    /* 1. Check argv[1] first for PE path, fall back to WINE32_PE_PATH env var */
    if (argc > 1) {
        pe_path = argv[1];
    } else {
        pe_path = my_getenv("WINE32_PE_PATH");
    }
    if (!pe_path) {
        INLINE_SYSCALL_WRITE_ERR(err_bad_env, sizeof(err_bad_env) - 1);
        INLINE_SYSCALL_EXIT_GROUP(1);
    }

    {
        const char *debug_level = my_getenv("MY_WINE_DEBUG_LEVEL");
        g_debug_level = parse_debug_level(debug_level);
    }

    /* Seed _acmdln so GetCommandLineA() returns the PE path.
     * Uses dll_copy_str to avoid the musl ifunc PLT resolution bug in
     * 32-bit static builds (same pattern used in ensure_argv_setup).
     * dll_copy_str always null-terminates so no separate terminator is needed. */
    dll_copy_str(_acmdln, pe_path, 256);

    /* 2. Map the PE image */
    g_loader.image_base = map_pe(pe_path);

    /* Resolve imports — must happen after map_pe() which sets g_loader.is_32bit
     * and before any other PE operations that depend on patched IAT entries. */
    init_msvcrt_imports();       /* Fill dynamic msvcrt entries (no-op under MY_WINE32) */
    init_import_table();         /* Sort import_table for binary search */
    if (resolve_imports(g_loader.image_base, &g_nt_headers) != 0) {
        INLINE_SYSCALL_WRITE_ERR(err_import, sizeof(err_import) - 1);
        INLINE_SYSCALL_EXIT_GROUP(1);
    }

    /* Debug: verify IAT entry for LoadLibraryA is non-zero */
    if (g_debug_level >= 3) {
        const char msg_iat[] = "pe32_entry: imports resolved, testing IAT entry\n";
        INLINE_SYSCALL_WRITE(2, msg_iat, sizeof(msg_iat) - 1);

        void *base = g_loader.image_base;
        IMAGE_DATA_DIRECTORY imp_dir;
        void *ll_addr = (void *)(uintptr_t)0;
        void *ll_iat_ptr = NULL; /* pointer to the IAT cell for LoadLibraryA */
        if (pe_get_import_dir(&g_nt_headers, &imp_dir) && imp_dir.VirtualAddress != 0) {
            uint32_t import_rva = imp_dir.VirtualAddress;
            IMAGE_IMPORT_DESCRIPTOR *desc =
                pe_rva_to_ptr(base, &g_nt_headers, import_rva,
                              sizeof(IMAGE_IMPORT_DESCRIPTOR));
            uint32_t desc_offset = 0;
            while (desc != NULL &&
                   desc_offset + sizeof(IMAGE_IMPORT_DESCRIPTOR) <= imp_dir.Size &&
                   desc->Name != 0) {
                const char *dll_name =
                    pe_rva_to_ptr(base, &g_nt_headers, desc->Name, 1);
                if (dll_name == NULL) break;
                /* Case-insensitive DLL name check (PE may use "KERNEL32.dll") */
                if (dll_strcasecmp(dll_name, "kernel32.dll") == 0) {
                    uint32_t ilt_rva = desc->u1.OriginalFirstThunk != 0
                                       ? desc->u1.OriginalFirstThunk
                                       : desc->FirstThunk;
                    uint8_t *orig_base = pe_rva_to_ptr(base, &g_nt_headers,
                                                       ilt_rva, sizeof(uint32_t));
                    uint8_t *iat_base = pe_rva_to_ptr(base, &g_nt_headers,
                                                      desc->FirstThunk,
                                                      sizeof(uint32_t));
                    if (orig_base == NULL || iat_base == NULL) break;

                    /* Log IAT check addresses */
                    {
                        char buf[128];
                        int n = 0;
                        const char *p;
                        for (p = "IAT check: orig_base=0x"; *p && n < 120; ) buf[n++] = *p++;
                        for (int h = 7; h >= 0; h--) {
                            buf[n++] = "0123456789abcdef"[((uintptr_t)orig_base >> (h*4)) & 0xf];
                        }
                        for (p = ", iat_base=0x"; *p && n < 120; ) buf[n++] = *p++;
                        for (int h = 7; h >= 0; h--) {
                            buf[n++] = "0123456789abcdef"[((uintptr_t)iat_base >> (h*4)) & 0xf];
                        }
                        buf[n++] = '\n';
                        INLINE_SYSCALL_WRITE(2, buf, n);
                    }

                    for (int j = 0; ; j++) {
                        size_t thunk_off = (size_t)j * sizeof(uint32_t);
                        if (thunk_off / sizeof(uint32_t) != (size_t)j ||
                            thunk_off > SIZE_MAX - sizeof(uint32_t) ||
                            !pe_rva_range_is_valid(ilt_rva,
                                                   thunk_off + sizeof(uint32_t),
                                                   pe_size_of_image(&g_nt_headers)) ||
                            !pe_rva_range_is_valid(desc->FirstThunk,
                                                   thunk_off + sizeof(uint32_t),
                                                   pe_size_of_image(&g_nt_headers))) {
                            break;
                        }
                        uint32_t thunk_val = (uint32_t)*((uint32_t *)(orig_base + j * 4));
                        if (thunk_val == 0) break;

                        /* Log each ILT entry */
                        {
                            char buf[80];
                            int n = 0;
                            const char *p;
                            for (p = "  ILT entry j="; *p && n < 70; ) buf[n++] = *p++;
                            { int d = n; int v = j;
                              if (v >= 0) { buf[d++] = '0' + v % 10; v /= 10; }
                              if (v >= 0) { buf[d++] = '0' + v % 10; v /= 10; }
                              n = d;
                            }
                            for (p = ": thunk=0x"; *p && n < 70; ) buf[n++] = *p++;
                            for (int h = 7; h >= 0; h--) {
                                buf[n++] = "0123456789abcdef"[(thunk_val >> (h*4)) & 0xf];
                            }
                            buf[n++] = '\n';
                            INLINE_SYSCALL_WRITE(2, buf, n);
                        }

                        if (thunk_val & 0x80000000) continue;
                        IMAGE_IMPORT_BY_NAME *imp_name =
                            pe_rva_to_ptr(base, &g_nt_headers, thunk_val,
                                          sizeof(IMAGE_IMPORT_BY_NAME));
                        if (imp_name == NULL) break;
                        const char *fname = (const char *)imp_name->Name;
                        if (fname[0] == 'L' && fname[1] == 'o' && fname[2] == 'a' &&
                            fname[3] == 'd' && fname[4] == 'L' && fname[5] == 'i' &&
                            fname[6] == 'b' && fname[7] == 'r' && fname[8] == 'a' &&
                            fname[9] == 'r' && fname[10] == 'y' &&
                            fname[11] == 'A' && fname[12] == '\0') {
                            ll_addr = (void *)(uintptr_t)*(uint32_t *)(iat_base + j * 4);
                            ll_iat_ptr = (void *)(iat_base + j * 4);
                            /* Log the IAT value for LoadLibraryA */
                            {
                                char buf[80];
                                int n = 0;
                                const char *p;
                                uint32_t iat_val = *(uint32_t *)(iat_base + j * 4);
                                for (p = "  IAT["; *p && n < 70; ) buf[n++] = *p++;
                                { int d = n; int v = j;
                                  if (v >= 0) { buf[d++] = '0' + v % 10; v /= 10; }
                                  if (v >= 0) { buf[d++] = '0' + v % 10; v /= 10; }
                                  n = d;
                                }
                                for (p = "]=0x"; *p && n < 70; ) buf[n++] = *p++;
                                for (int h = 7; h >= 0; h--) {
                                    buf[n++] = "0123456789abcdef"[(iat_val >> (h*4)) & 0xf];
                                }
                                buf[n++] = '\n';
                                INLINE_SYSCALL_WRITE(2, buf, n);
                            }
                            break;
                        }
                    }
                    if (ll_addr) break;
                }
                desc_offset += sizeof(IMAGE_IMPORT_DESCRIPTOR);
                desc = pe_rva_to_ptr(base, &g_nt_headers,
                                     import_rva + desc_offset,
                                     sizeof(IMAGE_IMPORT_DESCRIPTOR));
            }
        }
        {
            char buf[48];
            int i = 0;
            const char *p = "IAT: LoadLibraryA=0x";
            while (*p) buf[i++] = *p++;
            for (int h = 7; h >= 0; h--) {
                uintptr_t v = (uintptr_t)ll_addr;
                buf[i++] = "0123456789abcdef"[(v >> (h * 4)) & 0xf];
            }
            buf[i++] = '\n';
            INLINE_SYSCALL_WRITE(2, buf, i);
        }

        /* Final readback: verify the IAT cell survived pass 2 */
        if (ll_iat_ptr) {
            uint32_t final_val = *(uint32_t *)ll_iat_ptr;
            char buf[64];
            int i = 0;
            const char *p = "IAT: LoadLibraryA (final)=0x";
            while (*p) buf[i++] = *p++;
            for (int h = 7; h >= 0; h--) {
                buf[i++] = "0123456789abcdef"[(final_val >> (h * 4)) & 0xf];
            }
            buf[i++] = '\n';
            INLINE_SYSCALL_WRITE(2, buf, i);
        }

        /* Scan .text for JMP thunks whose IAT target is 0x0 (unresolved) */
        {
            IMAGE_SECTION_HEADER *sections = get_image_sections(base, &g_nt_headers);
            int num_sections = pe_section_count(&g_nt_headers);
            uint64_t targets[MAX_THUNK_TARGETS];
            int num_targets = scan_rip_relative_jumps(base, &g_nt_headers, sections,
                                                       num_sections, targets, MAX_THUNK_TARGETS);
            int zero_count = 0;
            for (int t = 0; t < num_targets; t++) {
                uint64_t target_rva = targets[t];
                if (target_rva > UINT32_MAX) continue;
                uint32_t *target_ptr = pe_rva_to_ptr(base, &g_nt_headers,
                                                     (uint32_t)target_rva,
                                                     sizeof(uint32_t));
                if (target_ptr == NULL) continue;
                if ((uint32_t)*target_ptr == 0x0) {
                    zero_count++;
                    char buf[80];
                    int n = 0;
                    const char *p = "  ZERO thunk target at IAT RVA 0x";
                    for (; *p && n < 70; ) buf[n++] = *p++;
                    for (int h = 7; h >= 0; h--) {
                        buf[n++] = "0123456789abcdef"[(target_rva >> (h*4)) & 0xf];
                    }
                    buf[n++] = '\n';
                    INLINE_SYSCALL_WRITE(2, buf, n);
                }
            }
            {
                char buf[80];
                int n = 0;
                const char *p = "IAT thunk scan: ";
                for (; *p && n < 70; ) buf[n++] = *p++;
                { int d = n; int v = num_targets;
                  if (v >= 0) { buf[d++] = '0' + v % 10; v /= 10; }
                  if (v >= 0) { buf[d++] = '0' + v % 10; v /= 10; }
                  n = d;
                }
                for (p = " targets, "; *p && n < 70; ) buf[n++] = *p++;
                { int d = n; int v = zero_count;
                  if (v >= 0) { buf[d++] = '0' + v % 10; v /= 10; }
                  if (v >= 0) { buf[d++] = '0' + v % 10; v /= 10; }
                  n = d;
                }
                for (p = " zero"; *p && n < 70; ) buf[n++] = *p++;
                buf[n++] = '\n';
                INLINE_SYSCALL_WRITE(2, buf, n);
            }
        }
    }

    /* 3. Determine entry point */
    entry_rva = resolve_entry(pe_path);
    entry_abs = (uint32_t)(uintptr_t)g_loader.image_base + entry_rva;

    /* 4-5. Allocate and initialize TEB and PEB */
    void *peb = init_peb32(g_loader.image_base);
    void *teb = init_teb32(peb);

    /* Wire additional PEB fields: heap, params, LDR, OS version */
    wire_peb32_fields(peb, g_loader.image_base, &g_nt_headers, pe_path);

    /* Setup guest stack */
    void *stack_top = setup_stack(&g_nt_headers);
    if (!stack_top) {
        INLINE_SYSCALL_WRITE_ERR(err_stack, sizeof(err_stack) - 1);
        INLINE_SYSCALL_EXIT_GROUP(1);
    }

    /* 6-7. Generate thunks and set up signal handlers */
    prepare_dispatch();

    /* Allocate 32-bit argv/envp arrays (used by seed_bss_vars and entry jump) */
    ensure_argv_setup(pe_path);

    /* Pre-seed CRT globals in .bss */
    seed_bss_vars(g_loader.image_base, &g_nt_headers);

    /* 7. Set FS → TEB and jump to PE entry */
    setup_fs_and_jump(teb, pe_path, entry_abs, stack_top);
}
