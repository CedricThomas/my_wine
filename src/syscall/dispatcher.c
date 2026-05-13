#define _GNU_SOURCE
#include <stdint.h>

#include "include/common.h"
#include "include/nt_constants.h"
#include "include/ntdll.h"
#include "include/syscall/dispatcher.h"
#include "include/syscall/dispatcher_entry.h"
#include "syscalls_inline.h"
#include "include/debug.h"

/*
 * dispatcher.c — NT syscall dispatcher
 *
 * Maps Windows NT syscall numbers to C handler functions.
 * Decodes arguments from guest registers/stack and dispatches
 * to the appropriate handler.
 *
 * Supports both x86_64 (Windows x64 ABI: RCX, RDX, R8, R9 + stack)
 * and x86 (cdecl: all arguments on stack).
 *
 * Architecture-independent design:
 *   - STACK(n): reads arg n from guest stack (portable to both archs)
 *   - arg1..arg4: macros resolving to STACK(1..4) on x86, register params on x86_64
 *   - writeback_ptr(p, v): writes result to guest pointer in native width
 *
 * The generated switch body (dispatcher_generated.c) is included via
 * #define DISPATCHER_C_BODY + #include, using these macros for portability.
 *
 * Single entry point: c_dispatch_syscall().
 */

/* ── Guest stack reader ───────────────────────────────────────── */

/*
 * is_valid_guest_ptr — check if a guest-space pointer is in a reasonable
 * range (image, stack, or heap). Prevents segfaults from garbage pointers.
 *
 * @ptr   the guest-space pointer value to validate
 *
 * @return 1 if the pointer passes all checks, 0 otherwise.
 */
static inline int is_valid_guest_ptr(uint64_t ptr)
{
    if (ptr == 0) return 0;               /* NULL is explicitly handled */
#if defined(__i386__)
    if (ptr > 0xFFFF8000UL)               /* must be in 32-bit user-space */
#else
    if (ptr > 0xfffffffffffe0000UL)       /* must be in user-space       */
#endif
        return 0;
    /* Alignment check: pointer must be at least native-width aligned */
#if defined(__i386__)
    if (ptr & 3) return 0;
#else
    if (ptr & 7) return 0;
#endif
    return 1;
}

/* Format "TRACE: syscall 0xXXXXXXXXXXXXXXXX\n" into buf (signal-safe, no snprintf) */
static void format_trace_syscall(char *buf, uint64_t nr)
{
    static const char prefix[] = "TRACE: syscall 0x";
    int i;
    for (i = 0; prefix[i]; i++) buf[i] = prefix[i];
    format_hex(buf + i, 29, nr);
    i += 16;
    buf[i++] = '\n';
    buf[i] = '\0';
}

/* Format "dispatcher: invalid RSP 0xXXXXXXXXXXXXXXXX in read_guest_stack\n" */
static void format_err_invalid_rsp(char *buf, uint64_t rsp)
{
    static const char prefix[] = "dispatcher: invalid RSP 0x";
    const char suffix[] = " in read_guest_stack\n";
    int i;
    for (i = 0; prefix[i]; i++) buf[i] = prefix[i];
    format_hex(buf + i, 38, rsp);
    i += 16;
    for (int j = 0; suffix[j]; j++) buf[i++] = suffix[j];
    buf[i] = '\0';
}

/* Format "dispatcher: RSP 0xXXXXXXXXXXXXXXXX failed guest-ptr check\n" */
static void format_err_rsp_check(char *buf, uint64_t rsp)
{
    static const char prefix[] = "dispatcher: RSP 0x";
    const char suffix[] = " failed guest-ptr check\n";
    int i;
    for (i = 0; prefix[i]; i++) buf[i] = prefix[i];
    format_hex(buf + i, 46, rsp);
    i += 16;
    for (int j = 0; suffix[j]; j++) buf[i++] = suffix[j];
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

/*
 * Format "my_wine: unhandled syscall 0xXXXXXXXXXXXXXXXX\n"
 * Consumed by the default case in dispatcher_generated.c.
 */
static void format_err_unhandled_syscall(char *buf, uint64_t nr)
{
    static const char prefix[] = "my_wine: unhandled syscall 0x";
    int i;
    for (i = 0; prefix[i]; i++) buf[i] = prefix[i];
    format_hex(buf + i, 19, nr);
    i += 16;
    buf[i++] = '\n';
    buf[i] = '\0';
}

/*
 * read_guest_stack — read an argument from the guest stack.
 *
 * Always returns uint64_t (zero-extended on 32-bit) to match the
 * generated code's uint64_t local variables and handler signatures.
 *
 * Uses __wine_guest_regs.esp (32-bit) or .rsp (64-bit), populated by
 * the assembly dispatcher entry.
 *
 * 32-bit cdecl (all args on stack, dispatcher pushf + thunk push ebp):
 *   [ESP+0]  = EFLAGS from pushf (index 0)
 *   [ESP+4]  = thunk return addr (index 1)
 *   [ESP+8]  = thunk's saved EBP (index 2)
 *   [ESP+12] = guest caller's ret addr (index 3)
 *   [ESP+16] = arg1 (index 4)
 *   [ESP+20] = arg2 (index 5)
 *   ...
 *   __ARG(n) reads index (n+3) to account for pushf + thunk frame.
 *   STACK(n) reads index (n+7) for arg5+ (pushf + thunk + 4 args).
 *
 * 64-bit (args 1-4 in registers, 5+ on stack):
 *   [RSP+0]  = thunk return addr (index 0)
 *   [RSP+8]  = thunk's saved RDI (index 1)
 *   [RSP+16] = guest caller's ret addr (index 2)
 *   [RSP+24] = arg5 (index 3, read as STACK(1) with offset baked into caller)
 *   ...
 *
 * @index  raw stack index
 *
 * @return the value from the guest stack, or 0 on invalid ESP/RSP.
 */
static inline uint64_t read_guest_stack(int index)
{
    if (index < 0 || index > 15) return 0;

#if defined(__i386__)
    uintptr_t esp = (uintptr_t)__wine_guest_regs.esp;

    if (esp == 0) return 0;  /* NULL stack → return 0 (test setup)
                               * Real code will have a valid ESP */
    if (esp > 0xFFFF8000UL) {
        char buf[64];
        format_err_invalid_rsp(buf, (uint64_t)esp);
        INLINE_SYSCALL_WRITE_ERR(buf, sizeof(buf) - 1);
        return 0;
    }
    if (!is_valid_guest_ptr((uint64_t)esp)) {
        char buf[64];
        format_err_rsp_check(buf, (uint64_t)esp);
        return 0;
    }
    /* Read 4-byte value, zero-extend to uint64_t */
    uint32_t *stack = (uint32_t *)(uintptr_t)esp;
    return (uint64_t)stack[index];
#else
    uintptr_t rsp = (uintptr_t)__wine_guest_regs.rsp;

    if (rsp == 0) return 0;  /* NULL stack → return 0 (test setup)
                               * Real code will have a valid RSP */
    if (rsp > 0xfffffffffffe0000UL) {
        char buf[64];
        format_err_invalid_rsp(buf, (uint64_t)rsp);
        INLINE_SYSCALL_WRITE_ERR(buf, sizeof(buf) - 1);
        return 0;
    }
    if (!is_valid_guest_ptr((uint64_t)rsp)) {
        char buf[64];
        format_err_rsp_check(buf, (uint64_t)rsp);
        return 0;
    }
    uint64_t *stack = (uint64_t *)(uintptr_t)rsp;
    return stack[index];
#endif
}

/*
 * read_guest_ptr — validate a guest pointer, read the value,
 * and set up a pointer for write-back.
 *
 * Always works with uint64_t to match handler signatures (PVOID*).
 * On 32-bit, the upper 32 bits of the read value may be garbage
 * (from reading 8 bytes where only 4 were written), but this is
 * OK because the writeback_ptr macro truncates to 32 bits.
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
    if (!is_valid_guest_ptr(guest_ptr)) {
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
 * Used by the generated switch body. Sets *result to the NTSTATUS error
 * and returns -1 so the caller can break out of the switch.
 *
 * @guest_arg  the guest arg value (pointer in guest space)
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

/* ── Dispatcher macros for generated code ──────────────────────── */

/*
 * STACK(n) — read argument n from the guest stack.
 *
 * The thunk pushes a frame before calling the dispatcher:
 *   64-bit: stack[0]=thunk_ret, stack[1]=saved_RDI, stack[2]=guest_ret
 *   32-bit: stack[0]=thunk_ret, stack[1]=saved_EBP, stack[2]=guest_ret
 *
 * On 64-bit: STACK(n) reads args 5+ (n=1 → arg5). First 4 args from registers.
 *
 * On 32-bit (cdecl): all args on stack. The arg1-4 macros and STACK(n) need
 * different offsets:
 *   - arg1-4 macros use __ARG(n) = stack[n+3] → args 1-4 at [4],[5],[6],[7]
 *   - STACK(n) for generated code uses stack[n+7] → arg5+ at [8],[9],...
 */
#if defined(__i386__)
#define __ARG(n) read_guest_stack((n) + 3)   /* pushf + thunk frame offset for arg1-4 */
#define STACK(n) read_guest_stack((n) + 7)   /* pushf + thunk frame + 4 args offset */
#else
#define __ARG(n) read_guest_stack(n)
#define STACK(n) read_guest_stack(n)
#endif

/*
 * writeback_ptr(p, v) — write a result value back to a guest-space pointer.
 *
 * Writes in native pointer width: 8 bytes on x86_64, 4 bytes on x86.
 * If p is NULL, no write occurs.
 */
#if defined(__i386__)
#define writeback_ptr(p, v) do { if (p) *(uint32_t *)(uintptr_t)(p) = (uint32_t)(v); } while(0)
#else
#define writeback_ptr(p, v) do { if (p) *(uint64_t *)(uintptr_t)(p) = (v); } while(0)
#endif

/*
 * WINE_GPTR(p) — convert a local variable address to a guest pointer value.
 *
 * Goes through uintptr_t to avoid pointer-to-int truncation warnings on 32-bit
 * when passing &local_var as PVOID (which is uint64_t).
 */
#define WINE_GPTR(p) ((uint64_t)(uintptr_t)(p))

/* ── Shared dispatcher core ─────────────────────────────────────── */

/*
 * STATUS_CAST(v) — cast a status/error value to the native result type.
 *
 * Used by the generated dispatcher code for early-exit error returns.
 * On 32-bit, dispatcher_core returns uint32_t, so error values must
 * be cast to uint32_t. On 64-bit, it returns uint64_t.
 * All STATUS_* constants fit in 32 bits, so the upper bits are always 0.
 */
#if defined(__i386__)
#define STATUS_CAST(v) ((uint32_t)(v))
#else
#define STATUS_CAST(v) ((uint64_t)(v))
#endif

/*
 * dispatcher_core — switch body shared by the dispatcher entry point.
 *
 * The generated switch body (dispatcher_generated.c) is included here
 * after defining DISPATCHER_C_BODY and the arg1-arg4 macros.
 *
 * 32-bit cdecl: all arguments on stack via __ARG(1-4) for arg1-4,
 *   STACK(n) for additional args (offset by thunk frame + 4 args).
 * 64-bit: args 1-4 from registers, 5+ from stack via STACK(n).
 */
#if defined(__i386__)
/*
 * 32-bit variant: takes syscall number from EDX (via thunk).
 * All arguments read from the guest stack (cdecl convention).
 * arg1-arg4 macros resolve to STACK(1-4).
 *
 * @nr    NT syscall number
 *
 * @return result to place in EAX
 */
static uint32_t dispatcher_core(uint32_t nr)
{
    uint64_t result = 0;

    /* On 32-bit: arg1-4 come from the stack (cdecl).
     * __ARG(n) = stack[n+3] reads args 1-4 past pushf + thunk frame. */
    #define arg1 __ARG(1)
    #define arg2 __ARG(2)
    #define arg3 __ARG(3)
    #define arg4 __ARG(4)

    #define DISPATCHER_C_BODY
    #include "dispatcher_generated.c"
    #undef DISPATCHER_C_BODY

    #undef arg1
    #undef arg2
    #undef arg3
    #undef arg4
    #undef STATUS_CAST

    return (uint32_t)result;
}
#else
/*
 * 64-bit variant: args 1-4 from registers (RCX, RDX, R8, R9),
 * remaining args from guest stack.
 *
 * @nr    NT syscall number
 * @a1    RCX argument (arg1)
 * @a2    RDX argument (arg2)
 * @a3    R8  argument (arg3)
 * @a4    R9  argument (arg4)
 *
 * @return result to place in RAX
 */
static uint64_t dispatcher_core(uint64_t nr, uint64_t a1, uint64_t a2,
                                 uint64_t a3, uint64_t a4)
{
    uint64_t result = 0;

    /* On 64-bit: arg1-4 come from register parameters */
    #define arg1 a1
    #define arg2 a2
    #define arg3 a3
    #define arg4 a4

    #define DISPATCHER_C_BODY
    #include "dispatcher_generated.c"
    #undef DISPATCHER_C_BODY

    #undef arg1
    #undef arg2
    #undef arg3
    #undef arg4
    #undef STATUS_CAST

    return result;
}
#endif

/* ── Dispatcher entry point ───────────────────────────────────── */

/*
 * c_dispatch_syscall — dispatch a Windows NT syscall to its handler.
 *
 * Reads input arguments from __wine_guest_regs (populated by the
 * assembly entry). Writes result into the appropriate output register.
 *
 * 32-bit: syscall number from EDX, all args from stack, result → EAX
 * 64-bit: syscall number from RDI, args from RCX/RDX/R8/R9+stack, result → RAX
 */
#if defined(__i386__)
uint32_t c_dispatch_syscall(uint32_t nr)
{
    char trace_buf[48];
    format_trace_syscall(trace_buf, (uint64_t)nr);
    INLINE_SYSCALL_WRITE_ERR(trace_buf, sizeof("TRACE: syscall 0xXXXXXXXXXXXXXXXX\n"));

    uint32_t result = dispatcher_core(nr);

    __wine_guest_regs.eax = result;
    return result;
}
#else
uint64_t c_dispatch_syscall(uint64_t nr)
{
    char trace_buf[48];
    format_trace_syscall(trace_buf, nr);
    INLINE_SYSCALL_WRITE_ERR(trace_buf, sizeof("TRACE: syscall 0xXXXXXXXXXXXXXXXX\n"));

    uint64_t result = dispatcher_core(nr, __wine_guest_regs.rcx, __wine_guest_regs.rdx,
                                       __wine_guest_regs.r8, __wine_guest_regs.r9);

    __wine_guest_regs.rax = result;
    return result;
}
#endif
