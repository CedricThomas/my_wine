/*
 * crt_globals.c — Global variable definitions for MSVCRT stubs.
 *
 * All CRT globals are now consolidated into wine_crt_state_t g_crt.
 * Previously-scattered individual globals (~20+) have been removed.
 */

#define _GNU_SOURCE

#include "msvcrt_priv.h"
#include "include/common.h"

/* ── Consolidated CRT global state ────────────────────────── */
/*
 * SINGLE-THREAD ONLY: g_crt is not safe for concurrent access.
 * g_crt.crt_ctx is written during patch_crt_refptrs() and read in
 * __getmainargs(). No synchronization is applied.
 */

wine_crt_state_t g_crt = {
    .ctor_list_stub = { 0 },
    .dtor_list_stub = { 0 },
};

/*
 * Self-referential pointer fixup.
 * _acmdln and __p__acmdln must point to the cmdline_storage buffer
 * inside g_crt, but we can't express &g_crt.cmdline_storage in a
 * compile-time initializer (the struct isn't fully materialized yet).
 * A constructor function runs before main() to fix these pointers.
 */
static __attribute__((constructor)) void crt_init_self_refs(void)
{
    g_crt.acmdln   = g_crt.cmdline_storage;
    g_crt.p_acmdln = g_crt.cmdline_storage;
}

/*
 * Wrapper functions that return pointer values.
 * These MUST be functions (not data) because MinGW CRT imports them
 * via JMP thunks — if the IAT contains a data address, the CPU will
 * try to execute it as instructions → SIGSEGV.
 * These are for the 64-bit build (crt_32_stub.c has its own for 32-bit).
 */
char *__p__acmdln_func(void) { return g_crt.acmdln; }
char **__initenv_func(void)  { return g_crt.initenv; }
char *__p__fmode_func(void)  { return (char*)&g_crt.fmode; }
char *__p__commode_func(void){ return (char*)&g_crt.commode; }
