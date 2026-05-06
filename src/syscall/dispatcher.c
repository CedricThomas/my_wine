#define _GNU_SOURCE
#include <stdint.h>

#include "include/common.h"
#include "include/nt_constants.h"
#include "include/ntdll.h"
#include "include/syscall/dispatcher.h"
#include "include/syscall/dispatcher_entry.h"  /* guest_regs, __wine_guest_regs */
#include "syscalls_inline.h"
#include "include/debug.h"

/*
 * dispatcher.c — NT syscall dispatcher
 *
 * Maps Windows NT syscall numbers to C handler functions.
 * Decodes arguments from guest registers using the x86_64 Windows
 * calling convention (RCX, RDX, R8, R9, ...) and dispatches
 * to the appropriate handler.
 *
 * Single entry point: c_dispatch_syscall().
 * Uses dispatcher_core() which includes the generated switch body.
 */

/* ── Guest stack reader ───────────────────────────────────────── */

/*
 * is_valid_guest_ptr — check if a guest-space pointer is in a reasonable
 * range (image, stack, or heap). Prevents segfaults from garbage pointers.
 *
 * @ptr   the guest-space pointer value to validate
 * @min_size  minimum number of bytes the caller intends to read through ptr
 *
 * @return 1 if the pointer passes all checks, 0 otherwise.
 */
static inline int is_valid_guest_ptr(uint64_t ptr, size_t min_size)
{
    if (ptr == 0) return 0;               /* NULL is explicitly handled */
    if (ptr > 0xfffffffffffe0000UL)       /* must be in user-space       */
        return 0;
    /* Alignment check: offset must be at least sizeof(uint64_t) aligned
     * so a uint64_t read/won't fault on most architectures.              */
    if (ptr & 7) return 0;
    (void)min_size; /* reserved for future range-checking against a known region */
    return 1;
}

/* Format "TRACE: syscall 0xXXXXXXXX\n" into buf (signal-safe, no snprintf) */
static void format_trace_syscall(char *buf, uint64_t nr)
{
    static const char hex[] = "0123456789ABCDEF";
    const char prefix[] = "TRACE: syscall 0x";
    int i;
    for (i = 0; prefix[i]; i++) buf[i] = prefix[i];
    for (int k = 7; k >= 0; k--) buf[i++] = hex[(nr >> (k * 4)) & 0xF];
    buf[i++] = '\n';
    buf[i] = '\0';
}

/* Format "dispatcher: invalid RSP 0xXXXXXXXX in read_guest_stack\n" */
static void format_err_invalid_rsp(char *buf, uint64_t rsp)
{
    static const char hex[] = "0123456789ABCDEF";
    const char p[] = "dispatcher: invalid RSP 0x";
    const char s[] = " in read_guest_stack\n";
    int i;
    for (i = 0; p[i]; i++) buf[i] = p[i];
    for (int k = 7; k >= 0; k--) buf[i++] = hex[(rsp >> (k * 4)) & 0xF];
    for (int j = 0; s[j]; j++) buf[i++] = s[j];
    buf[i] = '\0';
}

/* Format "dispatcher: RSP 0xXXXXXXXX failed guest-ptr check\n" */
static void format_err_rsp_check(char *buf, uint64_t rsp)
{
    static const char hex[] = "0123456789ABCDEF";
    const char p[] = "dispatcher: RSP 0x";
    const char s[] = " failed guest-ptr check\n";
    int i;
    for (i = 0; p[i]; i++) buf[i] = p[i];
    for (int k = 7; k >= 0; k--) buf[i++] = hex[(rsp >> (k * 4)) & 0xF];
    for (int j = 0; s[j]; j++) buf[i++] = s[j];
    buf[i] = '\0';
}

/* Format "dispatcher: invalid guest ptr 0xXXXXXXXX at NAME\n" */
static void format_err_guest_ptr(char *buf, int buf_size, uint64_t ptr, const char *name)
{
    static const char hex[] = "0123456789ABCDEF";
    const char p[] = "dispatcher: invalid guest ptr 0x";
    const char m[] = " at ";
    int i = 0;
    for (int j = 0; p[j]; j++) buf[i++] = p[j];
    for (int k = 7; k >= 0; k--) buf[i++] = hex[(ptr >> (k * 4)) & 0xF];
    for (int j = 0; m[j]; j++) buf[i++] = m[j];
    int n = 0;
    while (name[n] && i < buf_size - 2) { buf[i++] = name[n++]; }
    buf[i++] = '\n';
    buf[i] = '\0';
}

/* Format "my_wine: unhandled syscall 0xXXXXXXXX\n" */
static void format_err_unhandled_syscall(char *buf, uint64_t nr)
{
    static const char hex[] = "0123456789ABCDEF";
    const char p[] = "my_wine: unhandled syscall 0x";
    int i;
    for (i = 0; p[i]; i++) buf[i] = p[i];
    for (int k = 7; k >= 0; k--) buf[i++] = hex[(nr >> (k * 4)) & 0xF];
    buf[i++] = '\n';
    buf[i] = '\0';
}

/*
 * read_guest_stack — read an argument from the guest stack.
 *
 * Uses __wine_guest_regs.rsp (populated by the assembly dispatcher entry).
 *
 * In the Windows x64 ABI, args 5+ are on the stack:
 *   [RSP+0]  = return address (index 0)
 *   [RSP+8]  = arg5 (index 1)
 *   [RSP+16] = arg6 (index 2)
 *   ...
 *
 * @index  stack index (1 = arg5, 2 = arg6, ...)
 *
 * @return the 64-bit value from the guest stack, or 0 on invalid RSP.
 */
static inline uint64_t read_guest_stack(int index)
{
    if (index < 0 || index > 15) return 0;
    uintptr_t rsp = (uintptr_t)__wine_guest_regs.rsp;

    if (rsp == 0 || rsp > 0xfffffffffffe0000UL) {
        char buf[56];
        format_err_invalid_rsp(buf, (uint64_t)rsp);
        INLINE_SYSCALL_WRITE_ERR(buf, sizeof(buf) - 1);
        return 0;
    }
    if (!is_valid_guest_ptr((uint64_t)rsp, 8)) {
        char buf[51];
        format_err_rsp_check(buf, (uint64_t)rsp);
        INLINE_SYSCALL_WRITE_ERR(buf, sizeof(buf) - 1);
        return 0;
    }
    uint64_t *stack = (uint64_t *)(uintptr_t)rsp;
    return stack[index];
}

/*
 * read_guest_ptr — validate a guest pointer, read the uint64_t value,
 * and set up a pointer for write-back.
 *
 * @guest_ptr  the guest-space pointer to validate and read
 * @out_val    output: the value read from guest_ptr (0 if guest_ptr is NULL)
 * @out_ptr    output: a host pointer into the guest address (NULL if guest_ptr is NULL)
 * @name       for error messages
 *
 * @return 0 on success, STATUS_ACCESS_VIOLATION on invalid pointer.
 */
static int read_guest_ptr(uint64_t guest_ptr, uint64_t *out_val, void **out_ptr,
                          const char *name)
{
    if (guest_ptr == 0) {
        if (out_val) *out_val = 0;
        if (out_ptr) *out_ptr = NULL;
        return 0;
    }
    if (!is_valid_guest_ptr(guest_ptr, 8)) {
        char buf[64];
        format_err_guest_ptr(buf, sizeof(buf), guest_ptr, name);
        INLINE_SYSCALL_WRITE_ERR(buf, sizeof(buf) - 1);
        return STATUS_ACCESS_VIOLATION;
    }
    if (out_val) *out_val = *(uint64_t *)(uintptr_t)guest_ptr;
    if (out_ptr) *out_ptr = (void *)(uintptr_t)guest_ptr;
    return 0;
}

/* ── Dispatch helpers ─────────────────────────────────────────── */

/*
 * dispatch_ptr_inout — read a guest-space pointer (inout),
 * set result to error status on failure.
 *
 * Used by c_dispatch_syscall. Sets *result to the NTSTATUS error
 * and returns -1 so the caller can break out of the switch.
 *
 * @guest_arg  the guest arg register value (pointer in guest space)
 * @ptr_val    output: value read from guest ptr
 * @ptr_out    output: host pointer for write-back
 * @name       for error messages
 * @result     output: set to error NTSTATUS on failure
 *
 * @return 0 on success, -1 on error.
 */
static int dispatch_ptr_inout(uint64_t guest_arg, uint64_t *ptr_val,
                               void **ptr_out, const char *name,
                               uint64_t *result)
{
    int status = read_guest_ptr(guest_arg, ptr_val, ptr_out, name);
    if (status != 0) {
        *result = (uint64_t)status;
        return -1;
    }
    return 0;
}

/* ── Shared dispatcher core ─────────────────────────────────────── */

/*
 * dispatcher_core — switch body shared by the dispatcher entry point.
 *
 * @nr    NT syscall number
 * @arg1  RCX argument
 * @arg2  RDX argument
 * @arg3  R8 argument
 * @arg4  R9 argument
 *
 * @return result to place in RAX
 */
static uint64_t dispatcher_core(uint64_t nr, uint64_t arg1, uint64_t arg2,
                                 uint64_t arg3, uint64_t arg4)
{
    uint64_t result = 0;

    #define DISPATCHER_C_BODY
    #include "dispatcher_generated.c"
    #undef DISPATCHER_C_BODY

    return result;
}

/* ── Dispatcher entry point ───────────────────────────────────── */

/*
 * c_dispatch_syscall — dispatch a Windows NT syscall to its handler.
 *
 * Reads input arguments directly from __wine_guest_regs (populated by
 * the assembly entry). Writes result into __wine_guest_regs.rax.
 *
 * @nr  NT syscall number
 *
 * @return result to place in RAX
 */
uint64_t c_dispatch_syscall(uint64_t nr)
{
    char trace_buf[32];
    format_trace_syscall(trace_buf, nr);
    INLINE_SYSCALL_WRITE_ERR(trace_buf, sizeof("TRACE: syscall 0xXXXXXXXX\n"));

    uint64_t result = dispatcher_core(nr, __wine_guest_regs.rcx, __wine_guest_regs.rdx,
                                       __wine_guest_regs.r8, __wine_guest_regs.r9);

    __wine_guest_regs.rax = result;
    return result;
}
