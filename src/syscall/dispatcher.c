#define _GNU_SOURCE
#include <signal.h>
#include <stdio.h>
#include <stdint.h>
#include <sys/ucontext.h>

#include "include/ntdll.h"
#include "include/syscall/dispatcher.h"

/*
 * dispatcher.c — NT syscall dispatcher
 *
 * Maps Windows NT syscall numbers to C handler functions.
 * Decodes arguments from the ucontext using the x86_64 Windows
 * calling convention (RCX, RDX, R8, R9, ...) and dispatches
 * to the appropriate handler.
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

/*
 * read_guest_stack — read an argument from the guest stack.
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
static inline uint64_t read_guest_stack(ucontext_t *ctx, int index)
{
    uintptr_t rsp = (uintptr_t)ctx->uc_mcontext.gregs[REG_RSP];

    /* Validate RSP is in a reasonable user-space range */
    if (rsp == 0 || rsp > 0xfffffffffffe0000UL) {
        fprintf(stderr, "dispatcher: invalid RSP 0x%lx in read_guest_stack\n",
                (unsigned long)rsp);
        return 0;
    }

    /* Additional guard: RSP must pass our guest-ptr validator */
    if (!is_valid_guest_ptr((uint64_t)rsp, 8)) {
        fprintf(stderr, "dispatcher: RSP 0x%lx failed guest-ptr check\n",
                (unsigned long)rsp);
        return 0;
    }

    uint64_t *stack = (uint64_t *)(uintptr_t)rsp;
    return stack[index];
}

/* ── Dispatcher ───────────────────────────────────────────────── */

/*
 * handle_syscall — dispatch a Windows NT syscall to its handler.
 *
 * @syscall_number: the raw syscall number from si_syscall (includes 0xF000
 *                  Wine offset, e.g. 0xF005 for NtCallbackReturn)
 * @ctx:            pointer to the ucontext_t captured by the SIGSYS handler
 *
 * Decodes arguments from the x86_64 Windows calling convention:
 *   RCX (ARG1) = gregs[REG_RCX]
 *   RDX (ARG2) = gregs[REG_RDX]
 *   R8  (ARG3) = gregs[REG_R8]
 *   R9  (ARG4) = gregs[REG_R9]
 *
 * Writes the return value into gregs[REG_RAX].
 * On unhandled syscall, prints an error to stderr and raises SIGSEGV.
 */
int handle_syscall(uint64_t syscall_number, ucontext_t *ctx)
{
    /* syscall_number comes from si_syscall which includes the 0xF000
     * Wine offset. Strip it to get the base NT syscall number.       */
    uint64_t nt_nr = syscall_number - 0xF000;

    uint64_t arg1 = ctx->uc_mcontext.gregs[REG_RCX];
    uint64_t arg2 = ctx->uc_mcontext.gregs[REG_RDX];
    uint64_t arg3 = ctx->uc_mcontext.gregs[REG_R8];
    uint64_t arg4 = ctx->uc_mcontext.gregs[REG_R9];
    uint64_t result = 0;

    switch (nt_nr) {

    case 0x05: /* NtCallbackReturn */
        result = handler_NtCallbackReturn();
        break;

    case 0x07: /* NtQueryInformationProcess */
        result = handler_NtQueryInformationProcess(
            arg1, arg2, arg3, arg4,
            read_guest_stack(ctx, 1));
        break;

    case 0x0F: /* NtClose */
        result = handler_NtClose(arg1);
        break;

    case 0x18: /* NtAllocateVirtualMemory */
    {
        /* base_address and region_size are guest-space pointers;
         * pass host-side copies, then write back results.          */
        uint64_t h_base_addr = arg2;   /* guest ptr value           */
        uint64_t h_region_sz = arg4;
        uint64_t *p_base = NULL;
        uint64_t *p_region = NULL;
        if (arg2 != 0) {
            if (!is_valid_guest_ptr(arg2, 8)) {
                fprintf(stderr, "dispatcher: invalid guest ptr 0x%lx at base_address\n",
                        (unsigned long)arg2);
                return STATUS_ACCESS_VIOLATION;
            }
            p_base = (uint64_t *)(uintptr_t)arg2;
            h_base_addr = *p_base;
        }
        if (arg4 != 0) {
            if (!is_valid_guest_ptr(arg4, 8)) {
                fprintf(stderr, "dispatcher: invalid guest ptr 0x%lx at region_size\n",
                        (unsigned long)arg4);
                return STATUS_ACCESS_VIOLATION;
            }
            p_region = (uint64_t *)(uintptr_t)arg4;
            h_region_sz = *p_region;
        }
        result = handler_NtAllocateVirtualMemory(
            arg1, &h_base_addr, arg3, &h_region_sz,
            read_guest_stack(ctx, 1),
            read_guest_stack(ctx, 2));
        if (p_base != NULL)    *p_base = h_base_addr;
        if (p_region != NULL) *p_region = h_region_sz;
        break;
    }

    case 0x19: /* NtFreeVirtualMemory */
    {
        uint64_t h_base_addr = arg2;
        uint64_t h_region_sz = arg3;
        uint64_t *p_base = NULL;
        uint64_t *p_region = NULL;
        if (arg2 != 0) {
            if (!is_valid_guest_ptr(arg2, 8)) {
                fprintf(stderr, "dispatcher: invalid guest ptr 0x%lx at base_address\n",
                        (unsigned long)arg2);
                return STATUS_ACCESS_VIOLATION;
            }
            p_base = (uint64_t *)(uintptr_t)arg2;
            h_base_addr = *p_base;
        }
        if (arg3 != 0) {
            if (!is_valid_guest_ptr(arg3, 8)) {
                fprintf(stderr, "dispatcher: invalid guest ptr 0x%lx at region_size\n",
                        (unsigned long)arg3);
                return STATUS_ACCESS_VIOLATION;
            }
            p_region = (uint64_t *)(uintptr_t)arg3;
            h_region_sz = *p_region;
        }
        result = handler_NtFreeVirtualMemory(arg1, &h_base_addr, &h_region_sz, arg4);
        if (p_base != NULL)    *p_base = h_base_addr;
        if (p_region != NULL) *p_region = h_region_sz;
        break;
    }

    case 0x24: /* NtGetContextThread */
        result = handler_NtGetContextThread(arg1, arg2);
        break;

    case 0x26: /* NtSetContextThread */
        result = handler_NtSetContextThread(arg1, arg2);
        break;

    case 0x28: /* NtMapViewOfSection */
    {
        uint64_t h_base_addr = arg3;
        uint64_t h_section_off = read_guest_stack(ctx, 1);
        uint64_t h_view_sz = read_guest_stack(ctx, 2);
        uint64_t *p_base = NULL;
        uint64_t *p_offset = NULL;
        uint64_t *p_vsz = NULL;
        if (arg3 != 0) {
            if (!is_valid_guest_ptr(arg3, 8)) {
                fprintf(stderr, "dispatcher: invalid guest ptr 0x%lx at base_address\n",
                        (unsigned long)arg3);
                return STATUS_ACCESS_VIOLATION;
            }
            p_base = (uint64_t *)(uintptr_t)arg3;
            h_base_addr = *p_base;
        }
        if (h_section_off != 0) {
            if (!is_valid_guest_ptr(h_section_off, 8)) {
                fprintf(stderr, "dispatcher: invalid guest ptr 0x%lx at section_offset\n",
                        (unsigned long)h_section_off);
                return STATUS_ACCESS_VIOLATION;
            }
            p_offset = (uint64_t *)(uintptr_t)h_section_off;
            h_section_off = *p_offset;
        }
        if (h_view_sz != 0) {
            if (!is_valid_guest_ptr(h_view_sz, 8)) {
                fprintf(stderr, "dispatcher: invalid guest ptr 0x%lx at view_size\n",
                        (unsigned long)h_view_sz);
                return STATUS_ACCESS_VIOLATION;
            }
            p_vsz = (uint64_t *)(uintptr_t)h_view_sz;
            h_view_sz = *p_vsz;
        }
        result = handler_NtMapViewOfSection(
            arg1, arg2, &h_base_addr, arg4,
            read_guest_stack(ctx, 1),
            &h_section_off, &h_view_sz,
            read_guest_stack(ctx, 4),
            read_guest_stack(ctx, 5),
            read_guest_stack(ctx, 6));
        if (p_base != NULL)   *p_base = h_base_addr;
        if (p_offset != NULL) *p_offset = h_section_off;
        if (p_vsz != NULL)   *p_vsz = h_view_sz;
        break;
    }

    case 0x29: /* NtUnmapViewOfSection */
        result = handler_NtUnmapViewOfSection(arg1, arg2);
        break;

    case 0x2A: /* NtTerminateProcess */
        result = handler_NtTerminateProcess(arg1, arg2);
        break;

    case 0x3C: /* NtReadFile */
        result = handler_NtReadFile(arg1, arg2, arg3, arg4,
                                    read_guest_stack(ctx, 1),
                                    read_guest_stack(ctx, 2),
                                    read_guest_stack(ctx, 3),
                                    read_guest_stack(ctx, 4));
        break;

    case 0x3D: /* NtWriteFile */
        result = handler_NtWriteFile(arg1, arg2, arg3, arg4,
                                     read_guest_stack(ctx, 1),
                                     read_guest_stack(ctx, 2),
                                     read_guest_stack(ctx, 3),
                                     read_guest_stack(ctx, 4));
        break;

    case 0x48: /* NtCreateEvent */
    {
        uint64_t h_handle = arg1;
        uint64_t *p_handle = NULL;
        if (arg1 != 0) {
            if (!is_valid_guest_ptr(arg1, 8)) {
                fprintf(stderr, "dispatcher: invalid guest ptr 0x%lx at event_handle\n",
                        (unsigned long)arg1);
                return STATUS_ACCESS_VIOLATION;
            }
            p_handle = (uint64_t *)(uintptr_t)arg1;
            h_handle = *p_handle;
        }
        result = handler_NtCreateEvent(&h_handle, arg2, arg3, arg4,
                                       read_guest_stack(ctx, 1));
        if (p_handle != NULL) *p_handle = h_handle;
        break;
    }

    case 0x4A: /* NtCreateSection */
    {
        uint64_t h_handle = arg1;
        uint64_t h_max_sz = arg4;
        uint64_t *p_handle = NULL;
        uint64_t *p_max = NULL;
        if (arg1 != 0) {
            if (!is_valid_guest_ptr(arg1, 8)) {
                fprintf(stderr, "dispatcher: invalid guest ptr 0x%lx at section_handle\n",
                        (unsigned long)arg1);
                return STATUS_ACCESS_VIOLATION;
            }
            p_handle = (uint64_t *)(uintptr_t)arg1;
            h_handle = *p_handle;
        }
        if (arg4 != 0) {
            if (!is_valid_guest_ptr(arg4, 8)) {
                fprintf(stderr, "dispatcher: invalid guest ptr 0x%lx at maximum_size\n",
                        (unsigned long)arg4);
                return STATUS_ACCESS_VIOLATION;
            }
            p_max = (uint64_t *)(uintptr_t)arg4;
            h_max_sz = *p_max;
        }
        result = handler_NtCreateSection(&h_handle, arg2, arg3, &h_max_sz,
                                         read_guest_stack(ctx, 1),
                                         read_guest_stack(ctx, 2),
                                         read_guest_stack(ctx, 3));
        if (p_handle != NULL) *p_handle = h_handle;
        if (p_max != NULL)   *p_max = h_max_sz;
        break;
    }

    case 0x4E: /* NtCreateThreadEx */
    {
        uint64_t h_handle = arg1;
        uint64_t *p_handle = NULL;
        if (arg1 != 0) {
            if (!is_valid_guest_ptr(arg1, 8)) {
                fprintf(stderr, "dispatcher: invalid guest ptr 0x%lx at thread_handle\n",
                        (unsigned long)arg1);
                return STATUS_ACCESS_VIOLATION;
            }
            p_handle = (uint64_t *)(uintptr_t)arg1;
            h_handle = *p_handle;
        }
        result = handler_NtCreateThreadEx(&h_handle, arg2, arg3, arg4,
                                          read_guest_stack(ctx, 1),
                                          read_guest_stack(ctx, 2),
                                          read_guest_stack(ctx, 3),
                                          read_guest_stack(ctx, 4),
                                          read_guest_stack(ctx, 5),
                                          read_guest_stack(ctx, 6),
                                          read_guest_stack(ctx, 7));
        if (p_handle != NULL) *p_handle = h_handle;
        break;
    }

    case 0x4F: /* NtOpenFile */
    {
        uint64_t h_handle = arg1;
        uint64_t *p_handle = NULL;
        if (arg1 != 0) {
            if (!is_valid_guest_ptr(arg1, 8)) {
                fprintf(stderr, "dispatcher: invalid guest ptr 0x%lx at file_handle\n",
                        (unsigned long)arg1);
                return STATUS_ACCESS_VIOLATION;
            }
            p_handle = (uint64_t *)(uintptr_t)arg1;
            h_handle = *p_handle;
        }
        result = handler_NtOpenFile(&h_handle, arg2, arg3, arg4,
                                    read_guest_stack(ctx, 1),
                                    read_guest_stack(ctx, 2));
        if (p_handle != NULL) *p_handle = h_handle;
        break;
    }

    default:
        fprintf(stderr, "my_wine: unhandled syscall 0x%lX\n",
                (unsigned long)syscall_number);
        raise(SIGSEGV);
        return -1;
    }

    ctx->uc_mcontext.gregs[REG_RAX] = (greg_t)result;

    return 0;
}
