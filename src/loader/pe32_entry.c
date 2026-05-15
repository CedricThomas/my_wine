/*
 * pe32_entry.c — 32-bit ELF entry point for my_wine32
 *
 * This is the C entry point for the 32-bit backend selected by the
 * my_wine wrapper for PE32 images.
 *
 * The 32-bit backend is a standalone process that independently:
 *   1. Reads the PE file path from argv[1], with WINE32_PE_PATH as a fallback
 *   2. Opens and maps the PE image at the preferred (or default 0x00400000) base
 *   3. Allocates TEB at fixed address 0x7FFDE000 and PEB at 0x7FFDF000
 *   4. Initializes TEB/PEB (self-references, PEB pointer, image base)
 *   5. Generates 32-bit syscall thunks
 *   6. Installs POSIX crash signal handlers
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
#include "src/pe_priv.h"
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
#include "loader_state.h"
#include "pe32_process.h"
#include "include/syscall_safe_utils.h"
#include "../heap/wine_heap.h"

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
extern void install_crash_signal_handlers(void);
extern void seh_crash_handler(void *, void *, void *, void *);

/* Declaration from pe32_run_guest.S */

/* _acmdln — command-line string buffer from crt_32_stub.c (256 bytes).
 * Seeded in main() so GetCommandLineA() returns the PE path. */
extern char _acmdln[];
extern void pe32_run_guest(uint32_t entry_abs, void *stack_top) __attribute__((noreturn));

/* ── Error messages (null-terminated, written via syscall to stderr) ── */
static const char err_bad_path[]   = "my_wine32: missing PE path\n";
static const char err_map[]        = "my_wine32: failed to map PE image\n";
static const char err_teb[]        = "my_wine32: failed to allocate TEB\n";
static const char err_peb[]        = "my_wine32: failed to allocate PEB\n";
static const char err_unix_stack[] = "my_wine32: failed to setup UNIX stack\n";
static const char err_thunks[]     = "my_wine32: failed to generate thunks\n";
static const char err_stack[]      = "my_wine32: failed to setup guest stack\n";
static const char err_fs[]         = "my_wine32: set_thread_area (FS→TEB) failed\n";
static const char err_import[]     = "my_wine32: import resolution failed\n";

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
 * extract_entry_from_entry_point — extract the user entry function RVA
 * from the Watcom CRT startup pattern at the PE AddressOfEntryPoint.
 *
 * Watcom CRT entry stub (at AddressOfEntryPoint):
 *   c7 05 [disp32] [imm32]  ; mov dword [disp32], imm32  (10 bytes)
 *   e9 [rel32]              ; jmp crt_init               (5 bytes)
 *   (or e8 [rel32] — call crt_init — used by some Watcom versions)
 *
 * The imm32 field is the absolute runtime address of the user entry
 * function (after relocation). Compute RVA = imm32 - image_base.
 * Validates that the resulting RVA falls within a code section.
 *
 * @param out_rva  output: extracted RVA
 * @return         1 on success, 0 on failure
 */
static int extract_entry_from_entry_point(uint32_t *out_rva)
{
    /* Only attempt when the active CRT is Watcom (the only one with this pattern) */
    const crt_module_t *mod = crt_get_active();
    if (mod == NULL || crt_module_type(mod) != CRT_TYPE_WATCOM)
        return 0;

    /* Read 15 bytes from the PE entry point:
     *   c7 05 [disp32] [imm32]  ; mov dword [disp32], imm32  (10 bytes)
     *   e9/e8 [rel32]           ; jmp/call crt_init           (5 bytes)
     * We validate both the mov and the jmp/call to confirm the Watcom pattern.
     * pe_rva_to_const_ptr already verifies the range is within mapped bounds. */
    uint32_t entry_rva = pe_entry_rva(&g_nt_headers);
    const uint8_t *p = pe_rva_to_const_ptr(g_loader.image_base, &g_nt_headers,
                                           entry_rva, 15);
    if (p == NULL)
        return 0;

    /* Verify Watcom CRT entry pattern: mov dword [disp32], imm32 ; jmp/call */
    if (p[0] != 0xc7 || p[1] != 0x05)
        return 0;

    /* Verify disp32 (bytes 2..5) is non-zero — a zero disp32 means
     * it's not writing anywhere meaningful. */
    uint32_t disp32 = (uint32_t)((uint32_t)p[2]       |
                                 ((uint32_t)p[3] << 8)  |
                                 ((uint32_t)p[4] << 16) |
                                 ((uint32_t)p[5] << 24));
    if (disp32 == 0)
        return 0;

    /* Verify byte 10 is 0xe9 (jmp) or 0xe8 (call) — different Watcom
     * versions use different instructions for the CRT init jump. */
    if (p[10] != 0xe9 && p[10] != 0xe8)
        return 0;

    /* Extract imm32 (bytes 6..9) — absolute runtime address of user entry */
    uint32_t imm32 = (uint32_t)((uint32_t)p[6]       |
                                ((uint32_t)p[7] << 8)  |
                                ((uint32_t)p[8] << 16) |
                                ((uint32_t)p[9] << 24));

    /* Convert absolute address to RVA */
    uint32_t base = (uint32_t)(uintptr_t)g_loader.image_base;
    if (imm32 < base)
        return 0;  /* underflow — not a valid relocated address */

    uint32_t rva = imm32 - base;

    /* Validate: the RVA must fall within a code section */
    IMAGE_SECTION_HEADER *sections =
        get_image_sections(g_loader.image_base, &g_nt_headers);
    const IMAGE_SECTION_HEADER *code = find_code_section(&g_nt_headers, sections);
    if (code == NULL)
        return 0;

    uint32_t code_start = code->VirtualAddress;
    /* Watcom can set VirtualSize=0; fall back to SizeOfRawData */
    uint32_t code_size  = code->Misc.VirtualSize;
    if (code_size == 0) code_size = code->SizeOfRawData;

    if (rva < code_start || code_size == 0)
        return 0;
    uint32_t code_end = code_start + code_size;
    if (rva >= code_end)
        return 0;

    *out_rva = rva;
    return 1;
}

/*
 * resolve_entry_symbol — returns the user entry point RVA.
 *
 * // Requires crt_set_active() to have been called before this
 *
 * For PE32: tries to find the entry symbol from the active CRT module
 * (e.g. _main for MinGW, main for Watcom) in the COFF symbol table
 * so we can bypass the CRT startup and jump directly to the user's
 * entry point. This avoids the CRT relocator, exception filter setup,
 * FPU reset, and other CRT init code that causes crashes in the
 * 32-bit loader (EIP=0x0 after _out returns).
 *
 * If COFF symbols are absent (stripped binaries like Watcom DOOM95),
 * tries extracting the entry RVA from the Watcom CRT startup pattern
 * at the PE AddressOfEntryPoint. If that also fails, falls back to
 * the PE AddressOfEntryPoint (which points to CRT startup code).
 *
 * @return entry_rva
 */
static uint32_t resolve_entry_symbol(const char *path)
{
    uint32_t entry_rva = pe_entry_rva(&g_nt_headers);
    uint32_t ptr_sym = pe_pointer_to_symbol_table(&g_nt_headers);
    uint32_t num_sym = pe_number_of_symbols(&g_nt_headers);

    const IMAGE_SECTION_HEADER *sections =
        get_image_sections(g_loader.image_base, &g_nt_headers);
    int num_sections = pe_section_count(&g_nt_headers);

    /* Get the active CRT module's entry symbols */
    const crt_module_t *mod = crt_get_active();
    const char *const *entry_syms = crt_entry_symbols(mod);

    /* If COFF symbols are available, try to look up entry symbols */
    if (ptr_sym != 0 && num_sym != 0) {
        IMAGE_SYMBOL *symbols = NULL;
        char *string_table = NULL;
        int sym_count = parse_symbol_table_from_file(path, &g_nt_headers,
                                                      &symbols, &string_table);
        if (sym_count > 0) {
            uint32_t main_rva = 0;

            if (entry_syms) {
                /* Try each CRT-specified entry symbol */
                for (int si = 0; entry_syms[si] != NULL; si++) {
                    main_rva = lookup_symbol_rva(symbols, sym_count, string_table,
                                                  sections, num_sections,
                                                  entry_syms[si]);
                    if (main_rva != 0) break;
                }
            }

            /* If CRT module didn't find one, try _main / main as fallback */
            if (main_rva == 0) {
                main_rva = lookup_symbol_rva(symbols, sym_count, string_table,
                                              sections, num_sections, "_main");
            }
            if (main_rva == 0) {
                main_rva = lookup_symbol_rva(symbols, sym_count, string_table,
                                              sections, num_sections, "main");
            }

            if (main_rva != 0) {
                entry_rva = main_rva;
            }

            // Only free symbols — string_table is a pointer into the same
            // combined malloc'd buffer (see parse_symbol_table_from_file).
            free(symbols);
        }
    }

    /* COFF symbols absent — try extracting entry from Watcom CRT startup pattern */
    if (ptr_sym == 0 || num_sym == 0) {
        uint32_t extracted_rva;
        if (extract_entry_from_entry_point(&extracted_rva)) {
            DEBUG_LEVEL(1, "watcom_entry_extract: user_func VA=0x%x -> RVA=0x%x",
                        (uint32_t)(uintptr_t)g_loader.image_base + extracted_rva,
                        extracted_rva);
            entry_rva = extracted_rva;
        }
    }

    return entry_rva;
}

/*
 * patch_crt_initialized — sets the _initialized CRT flag to 1.
 *
 * This ensures that when _main calls ___main (MinGW), the CRT init
 * returns immediately without running __do_global_ctors, which can
 * crash with inconsistent state in the 32-bit loader.
 *
 * Only applies to MinGW CRT. Watcom CRT already patches _initialized
 * in watcom_patch_refptrs; unknown CRT types are skipped to avoid
 * double writes.
 */
static void patch_crt_initialized(const char *path)
{
    const crt_module_t *mod = crt_get_active();
    crt_type_t type = mod ? crt_module_type(mod) : CRT_TYPE_UNKNOWN;
    if (type == CRT_TYPE_WATCOM || type == CRT_TYPE_UNKNOWN)
        return;

    uint32_t ptr_sym = pe_pointer_to_symbol_table(&g_nt_headers);
    uint32_t num_sym = pe_number_of_symbols(&g_nt_headers);

    if (ptr_sym == 0 || num_sym == 0) return;

    const IMAGE_SECTION_HEADER *sections =
        get_image_sections(g_loader.image_base, &g_nt_headers);
    int num_sections = pe_section_count(&g_nt_headers);

    IMAGE_SYMBOL *symbols = NULL;
    char *string_table = NULL;
    int sym_count = parse_symbol_table_from_file(path, &g_nt_headers,
                                                  &symbols, &string_table);
    if (sym_count <= 0) return;

    uint32_t init_rva = lookup_symbol_rva(symbols, sym_count, string_table,
                                           sections, num_sections,
                                           "_initialized");
    if (init_rva == 0) {
        const IMAGE_SECTION_HEADER *bss = find_section_by_name(&g_nt_headers,
                                                         sections, ".bss");
        if (bss)
            init_rva = bss->VirtualAddress + 0x40;
    }
    // Only free symbols — string_table is a pointer into the same
    // combined malloc'd buffer (see parse_symbol_table_from_file).
    free(symbols);

    if (init_rva == 0) return;

    uint8_t *addr = pe_rva_to_ptr(g_loader.image_base, &g_nt_headers,
                                  init_rva, sizeof(uint32_t));
    if (addr == NULL) return;

    uintptr_t page = (uintptr_t)addr & ~(uintptr_t)PAGE_MASK;
    long rc = INLINE_SYSCALL_MPROTECT((void *)page, PAGE_SIZE,
                                       PROT_READ | PROT_WRITE);
    if (rc == 0)
        *(uint32_t *)addr = 1;
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

    install_crash_signal_handlers();
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
    *(uint32_t *)(sp + 8) = pe32_argv_ptr();  /* argv = 32-bit array */
    *(uint32_t *)(sp + 12) = pe32_envp_ptr();

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
        INLINE_SYSCALL_WRITE_ERR(err_bad_path, sizeof(err_bad_path) - 1);
        INLINE_SYSCALL_EXIT_GROUP(1);
    }

    {
        const char *debug_level = my_getenv("MY_WINE_DEBUG_LEVEL");
        g_debug_level = parse_debug_level(debug_level);
    }

    /* Seed _acmdln so GetCommandLineA() returns the PE path.
     * Uses syscall_safe_copy_str to avoid the musl ifunc PLT resolution bug in
     * 32-bit static builds (same pattern used in ensure_argv_setup).
     * syscall_safe_copy_str always null-terminates so no separate terminator is needed. */
    syscall_safe_copy_str(_acmdln, pe_path, 256);

    /* 2. Map the PE image */
    g_loader.image_base = map_pe(pe_path);

    /* Detect CRT type, activate the module, and patch refptrs
     * before import resolution — mirrors the PE32+ path in main.c */
    {
        IMAGE_SECTION_HEADER *sections = get_image_sections(g_loader.image_base, &g_nt_headers);
        crt_type_t crt_type = crt_detect_type(pe_path, &g_nt_headers);
        const crt_module_t *mod = crt_get_module(crt_type);
        crt_set_active(mod);
        crt_patch_refptrs(mod, pe_path, g_loader.image_base, &g_nt_headers, sections);
    }

    /* Resolve imports — must happen after map_pe() which sets g_loader.is_32bit
     * and after CRT patching so that refptrs are resolved before import lookup. */
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
                if (syscall_safe_strcasecmp(dll_name, "kernel32.dll") == 0) {
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

    /* 3. Determine entry point and patch CRT _initialized flag */
    entry_rva = resolve_entry_symbol(pe_path);
    patch_crt_initialized(pe_path);
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

    /* Pre-seed CRT globals in .bss using the active CRT module if available */
    {
        IMAGE_SECTION_HEADER *sections = get_image_sections(g_loader.image_base, &g_nt_headers);
        const crt_module_t *active = crt_get_active();
        if (crt_has_seed_bss(active)) {
            crt_seed_bss(active, g_loader.image_base, &g_nt_headers, sections);
        } else {
            seed_pe32_bss_vars(g_loader.image_base, &g_nt_headers);
        }
    }

    /* 7. Set FS → TEB and jump to PE entry */
    setup_fs_and_jump(teb, pe_path, entry_abs, stack_top);
}
