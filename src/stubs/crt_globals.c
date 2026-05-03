/*
 * crt_globals.c — Global variable definitions for MSVCRT stubs.
 */

#define _GNU_SOURCE

#include "msvcrt_priv.h"

/* ── Global variables ──────────────────────────────────────── */

int __msvcrt_app_type = 0;
int _commode = 0;
int _fmode = 0;
char **_msvcrt_environ = NULL;

/* Set from main.c before jump_to_entry; used by __getmainargs and _acmdln */
char **g_guest_argv  = NULL;
char **g_guest_envp  = NULL;

char _cmdline_storage[4096];
char *_acmdln = _cmdline_storage;

/* Static variables for additional CRT refptr patches */
int native_startup_lock = 0;
int native_startup_state = 0;
int dowildcard_val = 0;
int newmode_val = 0;
uint64_t g_image_base_ref = 0;

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
uint64_t dyn_tls_callback_stub     = 0;
uint64_t mingw_excpt_handler_stub  = 0;
uint64_t xc_a_stub                 = 0;
uint64_t xc_z_stub                 = 0;

/*
 * Stub arrays for __CTOR_LIST__ / __DTOR_LIST__.
 * __do_global_ctors reads the first element: if 0, no constructors exist.
 * A single {0} entry means "empty list" — the CRT skips the loop.
 */
uint32_t ctor_list_stub[] = { 0 };
uint32_t dtor_list_stub[] = { 0 };

/*
 * __xi_a / __xi_z mark the constructor range. When equal, no constructors.
 * The CRT compares them: if __xi_a == __xi_z, skip __do_global_ctors.
 */
uint64_t xi_a_stub = 0;
uint64_t xi_z_stub = 0;

/*
 * __imp___initenv stub: the PE code does mov %r8,(%rax) to store envp.
 * This means __imp___initenv must point to a writable location where
 * envp is stored. We set this dynamically to point to PE's .bss envp.
 * Initialize to 0, fix up in patch_crt_refptrs.
 */
void **__imp___initenv_stub = 0;
