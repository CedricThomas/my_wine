#define _GNU_SOURCE
#include <stdint.h>
#include <sys/ucontext.h>

#include "include/common.h"
#include "include/nt_constants.h"
#include "include/ntdll.h"
#include "include/syscall/dispatcher.h"
#include "include/syscall/dispatcher_entry.h"  /* guest_regs, __wine_guest_regs */
#include "../syscalls_inline.h"
#include "include/debug.h"

/*
 * dispatcher.c — NT syscall dispatcher
 *
 * Maps Windows NT syscall numbers to C handler functions.
 * Decodes arguments from guest registers using the x86_64 Windows
 * calling convention (RCX, RDX, R8, R9, ...) and dispatches
 * to the appropriate handler.
 *
 * Two dispatch entry points:
 *   - c_dispatch_syscall(nr): new path, reads from __wine_guest_regs
 *     directly (single-process model, no ucontext).
 *   - handle_syscall(nr, ctx): legacy path, reads from ucontext_t.
 *     Kept for backward compatibility with existing tests.
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
 * Uses __wine_guest_regs.rsp as the base pointer (single-process model).
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
 * read_guest_stack_ctx — read an argument from the guest stack, using
 * the ucontext from the SIGSYS handler (legacy path for tests).
 *
 * In the Windows x64 ABI, args 5+ are placed on the stack after the
 * call instruction pushes the return address. At the point of the
 * syscall instruction:
 *   [RSP+0]   = return address
 *   [RSP+8]   = arg5  (index 1)
 *   [RSP+16]  = arg6  (index 2)
 *   [RSP+24]  = arg7  (index 3)
 *   ...
 *
 * The guest stack is mmap'd into our address space, so we can
 * dereference it directly. RSP is validated before dereference.
 */
static inline uint64_t read_guest_stack_ctx(ucontext_t *ctx, int index)
{
    if (index < 0 || index > 15) return 0;
    uintptr_t rsp = (uintptr_t)ctx->uc_mcontext.gregs[REG_RSP];

    /* Validate RSP is in a reasonable user-space range */
    if (rsp == 0 || rsp > 0xfffffffffffe0000UL) {
        char buf[56];
        format_err_invalid_rsp(buf, (uint64_t)rsp);
        INLINE_SYSCALL_WRITE_ERR(buf, sizeof(buf) - 1);
        return 0;
    }

    /* Additional guard: RSP must pass our guest-ptr validator */
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

/* ── C dispatcher (single-process, no ucontext) ────────────────── */

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

    uint64_t arg1 = __wine_guest_regs.rcx;
    uint64_t arg2 = __wine_guest_regs.rdx;
    uint64_t arg3 = __wine_guest_regs.r8;
    uint64_t arg4 = __wine_guest_regs.r9;
    uint64_t result = 0;

    switch (nr) {

    case NT_SYSCALL_CALLBACK_RETURN: /* NtCallbackReturn */
        result = handler_NtCallbackReturn();
        break;

    case NT_SYSCALL_QUERY_INFO_PROCESS: /* NtQueryInformationProcess */
    {
        uint64_t h_buffer = arg3;
        uint64_t h_ret_len = read_guest_stack(1);
        int status = read_guest_ptr(h_buffer, NULL, NULL, "buffer");
        if (status != 0) return (uint64_t)status;
        status = read_guest_ptr(h_ret_len, NULL, NULL, "return_length");
        if (status != 0) return (uint64_t)status;
        result = handler_NtQueryInformationProcess(
            arg1, arg2, h_buffer, arg4, h_ret_len);
        break;
    }

    case NT_SYSCALL_CLOSE: /* NtClose */
        result = handler_NtClose(arg1);
        break;

    case NT_SYSCALL_ALLOC_VM: /* NtAllocateVirtualMemory */
    {
        uint64_t h_base_addr = 0;
        uint64_t h_region_sz = 0;
        void *p_base = NULL;
        void *p_region = NULL;
        int status = read_guest_ptr(arg2, &h_base_addr, &p_base, "base_address");
        if (status != 0) return (uint64_t)status;
        status = read_guest_ptr(arg4, &h_region_sz, &p_region, "region_size");
        if (status != 0) return (uint64_t)status;
        result = handler_NtAllocateVirtualMemory(
            arg1, &h_base_addr, arg3, &h_region_sz,
            read_guest_stack(1),
            read_guest_stack(2));
        if (p_base)   *(uint64_t *)p_base = h_base_addr;
        if (p_region) *(uint64_t *)p_region = h_region_sz;
        break;
    }

    case NT_SYSCALL_FREE_VM: /* NtFreeVirtualMemory */
    {
        uint64_t h_base_addr = 0;
        uint64_t h_region_sz = 0;
        void *p_base = NULL;
        void *p_region = NULL;
        int status = read_guest_ptr(arg2, &h_base_addr, &p_base, "base_address");
        if (status != 0) return (uint64_t)status;
        status = read_guest_ptr(arg3, &h_region_sz, &p_region, "region_size");
        if (status != 0) return (uint64_t)status;
        result = handler_NtFreeVirtualMemory(arg1, &h_base_addr, &h_region_sz, arg4);
        if (p_base)   *(uint64_t *)p_base = h_base_addr;
        if (p_region) *(uint64_t *)p_region = h_region_sz;
        break;
    }

    case NT_SYSCALL_GET_CTX_THREAD: /* NtGetContextThread */
    {
        int status = read_guest_ptr(arg2, NULL, NULL, "context");
        if (status != 0) return (uint64_t)status;
        result = handler_NtGetContextThread(arg1, arg2);
        break;
    }

    case NT_SYSCALL_SET_CTX_THREAD: /* NtSetContextThread */
    {
        int status = read_guest_ptr(arg2, NULL, NULL, "context");
        if (status != 0) return (uint64_t)status;
        result = handler_NtSetContextThread(arg1, arg2);
        break;
    }

    case NT_SYSCALL_MAP_VIEW: /* NtMapViewOfSection */
    {
        uint64_t h_base_addr = 0;
        uint64_t h_section_off = read_guest_stack(1);
        uint64_t h_view_sz = read_guest_stack(2);
        void *p_base = NULL;
        void *p_offset = NULL;
        void *p_vsz = NULL;
        int status = read_guest_ptr(arg3, &h_base_addr, &p_base, "base_address");
        if (status != 0) return (uint64_t)status;
        status = read_guest_ptr(h_section_off, &h_section_off, &p_offset,
                                "section_offset");
        if (status != 0) return (uint64_t)status;
        status = read_guest_ptr(h_view_sz, &h_view_sz, &p_vsz, "view_size");
        if (status != 0) return (uint64_t)status;
        result = handler_NtMapViewOfSection(
            arg1, arg2, &h_base_addr, arg4,
            read_guest_stack(1),
            &h_section_off, &h_view_sz,
            read_guest_stack(4),
            read_guest_stack(5),
            read_guest_stack(6));
        if (p_base)   *(uint64_t *)p_base = h_base_addr;
        if (p_offset) *(uint64_t *)p_offset = h_section_off;
        if (p_vsz)    *(uint64_t *)p_vsz = h_view_sz;
        break;
    }

    case NT_SYSCALL_UNMAP_VIEW: /* NtUnmapViewOfSection */
        result = handler_NtUnmapViewOfSection(arg1, arg2);
        break;

    case NT_SYSCALL_QUERY_SYSTEM_TIME: /* NtQuerySystemTime */
    {
        uint64_t h_ft_val = 0;
        void *p_ft = NULL;
        int status = read_guest_ptr(arg1, &h_ft_val, &p_ft, "filetime_ptr");
        if (status != 0) return (uint64_t)status;
        result = handler_NtQuerySystemTime((PVOID)&h_ft_val);
        if (p_ft) *(uint64_t *)p_ft = h_ft_val;
        break;
    }

    case NT_SYSCALL_DELAY_EXECUTION: /* NtDelayExecution */
    {
        uint64_t h_timeout = 0;
        void *p_timeout = NULL;
        int status = read_guest_ptr(arg2, &h_timeout, &p_timeout, "timeout_ptr");
        if (status != 0) return (uint64_t)status;
        result = handler_NtDelayExecution(arg1, (PVOID)&h_timeout);
        if (p_timeout) *(uint64_t *)p_timeout = h_timeout;
        break;
    }

    case NT_SYSCALL_TERMINATE_PROCESS: /* NtTerminateProcess */
        result = handler_NtTerminateProcess(arg1, arg2);
        break;

    case NT_SYSCALL_READ_FILE: /* NtReadFile */
    {
        uint64_t h_buffer = read_guest_stack(1);
        uint64_t h_bytes_read = read_guest_stack(4);
        int status = read_guest_ptr(h_buffer, NULL, NULL, "buffer");
        if (status != 0) return (uint64_t)status;
        status = read_guest_ptr(h_bytes_read, NULL, NULL, "bytes_read");
        if (status != 0) return (uint64_t)status;
        result = handler_NtReadFile(arg1, arg2, arg3, arg4,
                                    h_buffer,
                                    read_guest_stack(2),
                                    read_guest_stack(3),
                                    h_bytes_read);
        break;
    }

    case NT_SYSCALL_WRITE_FILE: /* NtWriteFile */
    {
        uint64_t h_buffer = read_guest_stack(1);
        uint64_t h_bytes_written = read_guest_stack(4);
        int status = read_guest_ptr(h_buffer, NULL, NULL, "buffer");
        if (status != 0) return (uint64_t)status;
        status = read_guest_ptr(h_bytes_written, NULL, NULL, "bytes_written");
        if (status != 0) return (uint64_t)status;
        result = handler_NtWriteFile(arg1, arg2, arg3, arg4,
                                     h_buffer,
                                     read_guest_stack(2),
                                     read_guest_stack(3),
                                     h_bytes_written);
        break;
    }

    case NT_SYSCALL_CREATE_EVENT: /* NtCreateEvent */
    {
        uint64_t h_handle = 0;
        void *p_handle = NULL;
        int status = read_guest_ptr(arg1, &h_handle, &p_handle, "event_handle");
        if (status != 0) return (uint64_t)status;
        result = handler_NtCreateEvent(&h_handle, arg2, arg3, arg4,
                                       read_guest_stack(1));
        if (p_handle) *(uint64_t *)p_handle = h_handle;
        break;
    }

    case NT_SYSCALL_CREATE_SECTION: /* NtCreateSection */
    {
        uint64_t h_handle = 0;
        uint64_t h_max_sz = 0;
        void *p_handle = NULL;
        void *p_max = NULL;
        int status = read_guest_ptr(arg1, &h_handle, &p_handle, "section_handle");
        if (status != 0) return (uint64_t)status;
        status = read_guest_ptr(arg4, &h_max_sz, &p_max, "maximum_size");
        if (status != 0) return (uint64_t)status;
        result = handler_NtCreateSection(&h_handle, arg2, arg3, &h_max_sz,
                                         read_guest_stack(1),
                                         read_guest_stack(2),
                                         read_guest_stack(3));
        if (p_handle) *(uint64_t *)p_handle = h_handle;
        if (p_max)    *(uint64_t *)p_max = h_max_sz;
        break;
    }

    case NT_SYSCALL_CREATE_THREAD_EX: /* NtCreateThreadEx */
    {
        uint64_t h_handle = 0;
        void *p_handle = NULL;
        int status = read_guest_ptr(arg1, &h_handle, &p_handle, "thread_handle");
        if (status != 0) return (uint64_t)status;
        result = handler_NtCreateThreadEx(&h_handle, arg2, arg3, arg4,
                                          read_guest_stack(1),
                                          read_guest_stack(2),
                                          read_guest_stack(3),
                                          read_guest_stack(4),
                                          read_guest_stack(5),
                                          read_guest_stack(6),
                                          read_guest_stack(7));
        if (p_handle) *(uint64_t *)p_handle = h_handle;
        break;
    }

    case NT_SYSCALL_OPEN_FILE: /* NtOpenFile */
    {
        uint64_t h_handle = 0;
        void *p_handle = NULL;
        int status = read_guest_ptr(arg1, &h_handle, &p_handle, "file_handle");
        if (status != 0) return (uint64_t)status;
        status = read_guest_ptr(arg3, NULL, NULL, "object_attributes");
        if (status != 0) return (uint64_t)status;
        status = read_guest_ptr(arg4, NULL, NULL, "io_status_block");
        if (status != 0) return (uint64_t)status;
        result = handler_NtOpenFile(&h_handle, arg2, arg3, arg4,
                                    read_guest_stack(1),
                                    read_guest_stack(2));
        if (p_handle) *(uint64_t *)p_handle = h_handle;
        break;
    }

    case NT_SYSCALL_QUERY_PERFORMANCE_COUNTER: /* NtQueryPerformanceCounter */
    {
        uint64_t h_counter = 0;
        void *p_counter = NULL;
        int status = read_guest_ptr(arg1, &h_counter, &p_counter, "counter_ptr");
        if (status != 0) return (uint64_t)status;
        result = handler_NtQueryPerformanceCounter((PVOID)&h_counter);
        if (p_counter) *(uint64_t *)p_counter = h_counter;
        break;
    }

    case NT_SYSCALL_QUERY_PERFORMANCE_FREQUENCY: /* NtQueryPerformanceFrequency */
    {
        uint64_t h_freq = 0;
        void *p_freq = NULL;
        int status = read_guest_ptr(arg1, &h_freq, &p_freq, "frequency_ptr");
        if (status != 0) return (uint64_t)status;
        result = handler_NtQueryPerformanceFrequency((PVOID)&h_freq);
        if (p_freq) *(uint64_t *)p_freq = h_freq;
        break;
    }

    default:
        {
            char buf[39];
            format_err_unhandled_syscall(buf, nr);
            INLINE_SYSCALL_WRITE_ERR(buf, sizeof(buf) - 1);
        }
        result = STATUS_NOT_IMPLEMENTED;
        break;
    }

    __wine_guest_regs.rax = result;
    return result;
}

/* ── Legacy dispatcher (ucontext-based, for test compatibility) ── */

/*
 * handle_syscall — dispatch a Windows NT syscall to its handler.
 *
 * @syscall_number: the raw NT syscall number (passed directly by thunks,
 *                  e.g. 0x05 for NtCallbackReturn)
 * @ctx:            pointer to the ucontext_t captured by the thunk entry
 *
 * Decodes arguments from the x86_64 Windows calling convention:
 *   RCX (ARG1) = gregs[REG_RCX]
 *   RDX (ARG2) = gregs[REG_RDX]
 *   R8  (ARG3) = gregs[REG_R8]
 *   R9  (ARG4) = gregs[REG_R9]
 *
 * Writes the return value into gregs[REG_RAX].
 * On unhandled syscall, prints an error to stderr and returns STATUS_NOT_IMPLEMENTED.
 *
 * NOTE: This function is kept for backward compatibility with existing
 * tests that construct ucontext_t manually. The production path uses
 * c_dispatch_syscall() instead.
 */
int handle_syscall(uint64_t syscall_number, ucontext_t *ctx)
{
    /* Trace every syscall invocation to stderr via direct write syscall */
    char trace_buf[32];
    format_trace_syscall(trace_buf, syscall_number);
    INLINE_SYSCALL_WRITE_ERR(trace_buf, sizeof("TRACE: syscall 0xXXXXXXXX\n"));

    /* syscall_number is the raw NT syscall number (passed directly
     * by the thunks — no Wine offset).                               */
    uint64_t nt_nr = syscall_number;

    uint64_t arg1 = ctx->uc_mcontext.gregs[REG_RCX];
    uint64_t arg2 = ctx->uc_mcontext.gregs[REG_RDX];
    uint64_t arg3 = ctx->uc_mcontext.gregs[REG_R8];
    uint64_t arg4 = ctx->uc_mcontext.gregs[REG_R9];
    uint64_t result = 0;

    switch (nt_nr) {

    case NT_SYSCALL_CALLBACK_RETURN: /* NtCallbackReturn */
        result = handler_NtCallbackReturn();
        break;

    case NT_SYSCALL_QUERY_INFO_PROCESS: /* NtQueryInformationProcess */
    {
        uint64_t h_buffer = arg3;
        uint64_t h_ret_len = read_guest_stack_ctx(ctx, 1);
        int status = read_guest_ptr(h_buffer, NULL, NULL, "buffer");
        if (status != 0) return status;
        status = read_guest_ptr(h_ret_len, NULL, NULL, "return_length");
        if (status != 0) return status;
        result = handler_NtQueryInformationProcess(
            arg1, arg2, h_buffer, arg4, h_ret_len);
        break;
    }

    case NT_SYSCALL_CLOSE: /* NtClose */
        result = handler_NtClose(arg1);
        break;

    case NT_SYSCALL_ALLOC_VM: /* NtAllocateVirtualMemory */
    {
        uint64_t h_base_addr = 0;
        uint64_t h_region_sz = 0;
        void *p_base = NULL;
        void *p_region = NULL;
        int status = read_guest_ptr(arg2, &h_base_addr, &p_base, "base_address");
        if (status != 0) return status;
        status = read_guest_ptr(arg4, &h_region_sz, &p_region, "region_size");
        if (status != 0) return status;
        result = handler_NtAllocateVirtualMemory(
            arg1, &h_base_addr, arg3, &h_region_sz,
            read_guest_stack_ctx(ctx, 1),
            read_guest_stack_ctx(ctx, 2));
        if (p_base)   *(uint64_t *)p_base = h_base_addr;
        if (p_region) *(uint64_t *)p_region = h_region_sz;
        break;
    }

    case NT_SYSCALL_FREE_VM: /* NtFreeVirtualMemory */
    {
        uint64_t h_base_addr = 0;
        uint64_t h_region_sz = 0;
        void *p_base = NULL;
        void *p_region = NULL;
        int status = read_guest_ptr(arg2, &h_base_addr, &p_base, "base_address");
        if (status != 0) return status;
        status = read_guest_ptr(arg3, &h_region_sz, &p_region, "region_size");
        if (status != 0) return status;
        result = handler_NtFreeVirtualMemory(arg1, &h_base_addr, &h_region_sz, arg4);
        if (p_base)   *(uint64_t *)p_base = h_base_addr;
        if (p_region) *(uint64_t *)p_region = h_region_sz;
        break;
    }

    case NT_SYSCALL_GET_CTX_THREAD: /* NtGetContextThread */
    {
        int status = read_guest_ptr(arg2, NULL, NULL, "context");
        if (status != 0) return status;
        result = handler_NtGetContextThread(arg1, arg2);
        break;
    }

    case NT_SYSCALL_SET_CTX_THREAD: /* NtSetContextThread */
    {
        int status = read_guest_ptr(arg2, NULL, NULL, "context");
        if (status != 0) return status;
        result = handler_NtSetContextThread(arg1, arg2);
        break;
    }

    case NT_SYSCALL_MAP_VIEW: /* NtMapViewOfSection */
    {
        uint64_t h_base_addr = 0;
        uint64_t h_section_off = read_guest_stack_ctx(ctx, 1);
        uint64_t h_view_sz = read_guest_stack_ctx(ctx, 2);
        void *p_base = NULL;
        void *p_offset = NULL;
        void *p_vsz = NULL;
        int status = read_guest_ptr(arg3, &h_base_addr, &p_base, "base_address");
        if (status != 0) return status;
        status = read_guest_ptr(h_section_off, &h_section_off, &p_offset,
                                "section_offset");
        if (status != 0) return status;
        status = read_guest_ptr(h_view_sz, &h_view_sz, &p_vsz, "view_size");
        if (status != 0) return status;
        result = handler_NtMapViewOfSection(
            arg1, arg2, &h_base_addr, arg4,
            read_guest_stack_ctx(ctx, 1),
            &h_section_off, &h_view_sz,
            read_guest_stack_ctx(ctx, 4),
            read_guest_stack_ctx(ctx, 5),
            read_guest_stack_ctx(ctx, 6));
        if (p_base)   *(uint64_t *)p_base = h_base_addr;
        if (p_offset) *(uint64_t *)p_offset = h_section_off;
        if (p_vsz)    *(uint64_t *)p_vsz = h_view_sz;
        break;
    }

    case NT_SYSCALL_UNMAP_VIEW: /* NtUnmapViewOfSection */
        result = handler_NtUnmapViewOfSection(arg1, arg2);
        break;

    case NT_SYSCALL_QUERY_SYSTEM_TIME: /* NtQuerySystemTime */
    {
        uint64_t h_ft_val = 0;
        void *p_ft = NULL;
        int status = read_guest_ptr(arg1, &h_ft_val, &p_ft, "filetime_ptr");
        if (status != 0) return status;
        result = handler_NtQuerySystemTime((PVOID)&h_ft_val);
        if (p_ft) *(uint64_t *)p_ft = h_ft_val;
        break;
    }

    case NT_SYSCALL_DELAY_EXECUTION: /* NtDelayExecution */
    {
        uint64_t h_timeout = 0;
        void *p_timeout = NULL;
        int status = read_guest_ptr(arg2, &h_timeout, &p_timeout, "timeout_ptr");
        if (status != 0) return status;
        result = handler_NtDelayExecution(arg1, (PVOID)&h_timeout);
        if (p_timeout) *(uint64_t *)p_timeout = h_timeout;
        break;
    }

    case NT_SYSCALL_TERMINATE_PROCESS: /* NtTerminateProcess */
        result = handler_NtTerminateProcess(arg1, arg2);
        break;

    case NT_SYSCALL_READ_FILE: /* NtReadFile */
    {
        uint64_t h_buffer = read_guest_stack_ctx(ctx, 1);
        uint64_t h_bytes_read = read_guest_stack_ctx(ctx, 4);
        int status = read_guest_ptr(h_buffer, NULL, NULL, "buffer");
        if (status != 0) return status;
        status = read_guest_ptr(h_bytes_read, NULL, NULL, "bytes_read");
        if (status != 0) return status;
        result = handler_NtReadFile(arg1, arg2, arg3, arg4,
                                    h_buffer,
                                    read_guest_stack_ctx(ctx, 2),
                                    read_guest_stack_ctx(ctx, 3),
                                    h_bytes_read);
        break;
    }

    case NT_SYSCALL_WRITE_FILE: /* NtWriteFile */
    {
        uint64_t h_buffer = read_guest_stack_ctx(ctx, 1);
        uint64_t h_bytes_written = read_guest_stack_ctx(ctx, 4);
        int status = read_guest_ptr(h_buffer, NULL, NULL, "buffer");
        if (status != 0) return status;
        status = read_guest_ptr(h_bytes_written, NULL, NULL, "bytes_written");
        if (status != 0) return status;
        result = handler_NtWriteFile(arg1, arg2, arg3, arg4,
                                     h_buffer,
                                     read_guest_stack_ctx(ctx, 2),
                                     read_guest_stack_ctx(ctx, 3),
                                     h_bytes_written);
        break;
    }

    case NT_SYSCALL_CREATE_EVENT: /* NtCreateEvent */
    {
        uint64_t h_handle = 0;
        void *p_handle = NULL;
        int status = read_guest_ptr(arg1, &h_handle, &p_handle, "event_handle");
        if (status != 0) return status;
        result = handler_NtCreateEvent(&h_handle, arg2, arg3, arg4,
                                       read_guest_stack_ctx(ctx, 1));
        if (p_handle) *(uint64_t *)p_handle = h_handle;
        break;
    }

    case NT_SYSCALL_CREATE_SECTION: /* NtCreateSection */
    {
        uint64_t h_handle = 0;
        uint64_t h_max_sz = 0;
        void *p_handle = NULL;
        void *p_max = NULL;
        int status = read_guest_ptr(arg1, &h_handle, &p_handle, "section_handle");
        if (status != 0) return status;
        status = read_guest_ptr(arg4, &h_max_sz, &p_max, "maximum_size");
        if (status != 0) return status;
        result = handler_NtCreateSection(&h_handle, arg2, arg3, &h_max_sz,
                                         read_guest_stack_ctx(ctx, 1),
                                         read_guest_stack_ctx(ctx, 2),
                                         read_guest_stack_ctx(ctx, 3));
        if (p_handle) *(uint64_t *)p_handle = h_handle;
        if (p_max)    *(uint64_t *)p_max = h_max_sz;
        break;
    }

    case NT_SYSCALL_CREATE_THREAD_EX: /* NtCreateThreadEx */
    {
        uint64_t h_handle = 0;
        void *p_handle = NULL;
        int status = read_guest_ptr(arg1, &h_handle, &p_handle, "thread_handle");
        if (status != 0) return status;
        result = handler_NtCreateThreadEx(&h_handle, arg2, arg3, arg4,
                                          read_guest_stack_ctx(ctx, 1),
                                          read_guest_stack_ctx(ctx, 2),
                                          read_guest_stack_ctx(ctx, 3),
                                          read_guest_stack_ctx(ctx, 4),
                                          read_guest_stack_ctx(ctx, 5),
                                          read_guest_stack_ctx(ctx, 6),
                                          read_guest_stack_ctx(ctx, 7));
        if (p_handle) *(uint64_t *)p_handle = h_handle;
        break;
    }

    case NT_SYSCALL_OPEN_FILE: /* NtOpenFile */
    {
        uint64_t h_handle = 0;
        void *p_handle = NULL;
        int status = read_guest_ptr(arg1, &h_handle, &p_handle, "file_handle");
        if (status != 0) return status;
        status = read_guest_ptr(arg3, NULL, NULL, "object_attributes");
        if (status != 0) return status;
        status = read_guest_ptr(arg4, NULL, NULL, "io_status_block");
        if (status != 0) return status;
        result = handler_NtOpenFile(&h_handle, arg2, arg3, arg4,
                                    read_guest_stack_ctx(ctx, 1),
                                    read_guest_stack_ctx(ctx, 2));
        if (p_handle) *(uint64_t *)p_handle = h_handle;
        break;
    }

    case NT_SYSCALL_QUERY_PERFORMANCE_COUNTER: /* NtQueryPerformanceCounter */
    {
        uint64_t h_counter = 0;
        void *p_counter = NULL;
        int status = read_guest_ptr(arg1, &h_counter, &p_counter, "counter_ptr");
        if (status != 0) return status;
        result = handler_NtQueryPerformanceCounter((PVOID)&h_counter);
        if (p_counter) *(uint64_t *)p_counter = h_counter;
        break;
    }

    case NT_SYSCALL_QUERY_PERFORMANCE_FREQUENCY: /* NtQueryPerformanceFrequency */
    {
        uint64_t h_freq = 0;
        void *p_freq = NULL;
        int status = read_guest_ptr(arg1, &h_freq, &p_freq, "frequency_ptr");
        if (status != 0) return status;
        result = handler_NtQueryPerformanceFrequency((PVOID)&h_freq);
        if (p_freq) *(uint64_t *)p_freq = h_freq;
        break;
    }

    default:
        {
            char buf[39];
            format_err_unhandled_syscall(buf, syscall_number);
            INLINE_SYSCALL_WRITE_ERR(buf, sizeof(buf) - 1);
        }
        result = STATUS_NOT_IMPLEMENTED;
        break;
    }

    ctx->uc_mcontext.gregs[REG_RAX] = (greg_t)result;

    return 0;
}
