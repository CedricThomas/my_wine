/*
 * pe32_entry.c — 32-bit ELF entry point for my_wine32
 *
 * This is the C entry point for the 32-bit backend selected by the
 * my_wine wrapper for PE32 images.
 *
 * The 32-bit backend is a standalone process that independently:
 *   1. Reads the PE file path from argv[1]
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
 *
 * Cleanup note:
 * This file still owns too many concerns at once: PE32 bootstrap, entry
 * resolution, Doom95 compatibility shaping, TEB/PEB creation, and the final
 * FS/guest handoff. The current pass keeps behavior stable while making those
 * stages easier to see and split later.
 */

#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>

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
#include "pe32_bootstrap.h"
#include "pe32_doom95_compat.h"
#include "pe32_entry_resolve.h"
#include "pe32_guest_launch.h"
#include "pe32_process.h"
#include "include/syscall_safe_utils.h"

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

/* Module-level vars for NT headers (needed for setup_stack and other modules) */
static IMAGE_NT_HEADERS g_nt_headers;

static char g_doom95_basewad_option[] = "-basewad";
static char g_doom95_basewad_path[512];

/* ── Error messages (null-terminated, written via syscall to stderr) ── */
static const char err_bad_path[]   = "my_wine32: missing PE path\n";

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
    void *peb;
    void *teb;
    void *stack_top;

    /* Stage 1: input path selection and early runtime config */
    pe_path = pe32_resolve_path_or_null(argc, argv);
    if (!pe_path) {
        INLINE_SYSCALL_WRITE_ERR(err_bad_path, sizeof(err_bad_path) - 1);
        INLINE_SYSCALL_EXIT_GROUP(1);
    }

    pe32_init_runtime_debug_level();
    pe32_seed_command_line(pe_path);

    /* Stage 2: image map and CRT setup */
    g_loader.image_base = pe32_map_image_or_exit(pe_path, &g_nt_headers);
    g_loader.image_size = pe_size_of_image(&g_nt_headers);
    pe32_activate_crt(g_loader.image_base, &g_nt_headers, pe_path);
    pe32_resolve_imports_or_exit(g_loader.image_base, &g_nt_headers);
    pe32_debug_verify_import_state(g_loader.image_base, &g_nt_headers);

    /* 3. Determine entry point and patch CRT _initialized flag */
    entry_rva = pe32_resolve_entry_symbol(g_loader.image_base, &g_nt_headers,
                                          pe_path);
    pe32_patch_crt_initialized(g_loader.image_base, &g_nt_headers, pe_path);
    entry_abs = (uint32_t)(uintptr_t)g_loader.image_base + entry_rva;

    /* Stage 3: PE32 process state */
    peb = pe32_init_peb_or_exit(g_loader.image_base);
    teb = pe32_init_teb_or_exit(peb);

    /* Wire additional PEB fields: heap, params, LDR, OS version */
    wire_peb32_fields(peb, g_loader.image_base, &g_nt_headers, pe_path);

    stack_top = pe32_setup_guest_stack_or_exit(&g_nt_headers);

    /* Stage 4: dispatcher and guest-visible process state */
    pe32_prepare_dispatch_or_exit();

    /* Allocate 32-bit argv/envp arrays before CRT .bss seeding. */
    ensure_argv_setup(pe_path);

    pe32_seed_crt_bss(g_loader.image_base, &g_nt_headers);
    pe32_apply_doom95_runtime_compat(pe_path, g_loader.image_base, &g_nt_headers,
                                     g_doom95_basewad_path,
                                     sizeof(g_doom95_basewad_path),
                                     g_doom95_basewad_option);

    /* Stage 5: final selector switch and guest handoff */
    pe32_setup_fs_and_jump(teb, pe_path, entry_abs, stack_top);
}
