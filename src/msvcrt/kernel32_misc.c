#define _GNU_SOURCE

#include "kernel32_priv.h"
#include "msvcrt_priv.h"
#include "include/wine_abi.h"
#include "include/nt_constants.h"

/*
 * g_crt is declared in include/crt.h (included via msvcrt_priv.h) and defined
 * in crt_globals.c. For build targets that exclude crt_*.o (e.g. test_syscall_dispatch),
 * we declare it as weak so the build doesn't fail with undefined reference.
 * At runtime, we check the address to see if g_crt was actually linked.
 */
extern wine_crt_state_t g_crt __attribute__((weak));
extern void *g_argv_page;

/* Thread-local last-error code */
/* In PE32 mode, __thread uses GS-relative access but GS=0 (only FS is set to TEB).
 * Use a plain global — PE32 is single-threaded so no contention risk. */
uint32_t g_last_error = 0;

/* ── GetLastError ───────────────────────────────────────────── */

KERNEL32_STUB
uint32_t GetLastError(void)
{
    return g_last_error;
}

KERNEL32_STUB
int IsTNT(void)
{
    return 0;
}

KERNEL32_STUB
void *TlsGetValue(uint32_t dwTlsIndex)
{
    extern uint32_t g_tls_bitmap;
    extern void *g_tls_values[];

    if (dwTlsIndex >= 64 || (g_tls_bitmap & (1u << dwTlsIndex)) == 0)
        return FORCE_PTR_RETURN(NULL);
    return FORCE_PTR_RETURN(g_tls_values[dwTlsIndex]);
}

/* ── GetCommandLineA ───────────────────────────────────────── */
/*
 * Returns the command-line string for the current process.
 * In 32-bit mode, g_argv_page is allocated with MAP_32BIT (below 4GB)
 * and holds the PE path at offset 0, so the base pointer IS the string.
 * In 64-bit mode, g_crt.acmdln (from crt_globals) is fine since all addresses
 * are accessible to the guest.
 */
KERNEL32_STUB
const char *GetCommandLineA(void)
{
#ifdef __i386__
    if (g_argv_page) return FORCE_PTR_RETURN((const char *)g_argv_page);
    return FORCE_PTR_RETURN("");
#else
    if ((uintptr_t)&g_crt != 0 && g_crt.acmdln)
        return FORCE_PTR_RETURN(g_crt.acmdln);
    return FORCE_PTR_RETURN("");
#endif
}

/* ── GetEnvironmentStringsA ────────────────────────────────── */
/*
 * Returns the environment block for the current process.
 * Some Watcom PE32 startup code walks this block unconditionally, so returning
 * NULL crashes even when argv/envp were already pre-seeded elsewhere.
 * A minimal empty environment is two trailing NUL bytes.
 */
KERNEL32_STUB
char *GetEnvironmentStringsA(void)
{
    static char empty_env_block[2] = { '\0', '\0' };
    return FORCE_PTR_RETURN(empty_env_block);
}

/* ── __C_specific_handler ──────────────────────────────────── */
/*
 * This is the SEH (Structured Exception Handling) handler used by
 * MSVC for exception dispatch. For our minimal runtime we just
 * return ExceptionContinueSearch (1) to skip the handler.
 */
KERNEL32_STUB
uint64_t __C_specific_handler(uint64_t exception_record, uint64_t establisher_frame,
                               uint64_t context_record, uint64_t dispatcher_context,
                               uint64_t image_base, uint64_t module_data,
                               uint64_t count, uint64_t handlers, uint64_t lang_handler,
                               uint64_t user_handler)
{
    (void)exception_record;
    (void)establisher_frame;
    (void)context_record;
    (void)dispatcher_context;
    (void)image_base;
    (void)module_data;
    (void)count;
    (void)handlers;
    (void)lang_handler;
    (void)user_handler;
    /*
     * Return values:
     *   0 = ExceptionContinueExecution
     *   1 = ExceptionContinueSearch
     *   2 = ExceptionNestedException
     *   3 = ExceptionCollidedUnwind
     */
    return 1; /* ExceptionContinueSearch — skip this handler */
}

/* ── test stubs ──────────────────────────────────────────────── */
/*
 * Test stubs for validating FORCE_PTR_RETURN macro behavior.
 * Used to verify that pointer-returning stubs correctly force values
 * into RAX for guest code consumption.
 */
KERNEL32_STUB void *test_return_ptr(void) { return FORCE_PTR_RETURN((void *)0x12345678UL); }
KERNEL32_STUB void *test_return_ptr_arg(void *arg) { return FORCE_PTR_RETURN(arg ? arg : (void *)0xdeadbeefUL); }
