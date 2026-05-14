/*
 * wine_abi.h — ABI attribute macros for functions called from guest PE code.
 *
 * All functions that are invoked from Windows x64 guest binaries must use the
 * Microsoft x64 calling convention (ms_abi). Stack alignment is handled
 * manually since force_align_arg_pointer breaks with guest stacks above 4GB.
 */

#ifndef WINE_ABI_H
#define WINE_ABI_H

#include <stdint.h>

/*
 * GUEST_ABI — attribute for guest-facing function declarations in headers.
 * Must match WINE_STUB so declarations and definitions are compatible.
 */
#if defined(__i386__)
#define GUEST_ABI
#else
#define GUEST_ABI __attribute__((ms_abi))
#endif

/*
 * KERNEL32_ABI — 32-bit Windows API imports use stdcall, where the callee
 * pops stack arguments. MSVCRT imports remain cdecl on i386, so keep this
 * separate from GUEST_ABI/WINE_STUB.
 */
#if defined(__i386__)
#define KERNEL32_ABI __attribute__((stdcall))
#else
#define KERNEL32_ABI __attribute__((ms_abi))
#endif

/*
 * WINE_STUB — marks a non-static function as using the guest calling
 * convention. Use for all exported stub functions called from guest PE code
 * (kernel32, msvcrt, ntdll exports).
 *
 * x86_64: Microsoft x64 calling convention (ms_abi).
 * i386:   standard cdecl (all args on stack).
 *
 * NOTE: We cannot use force_align_arg_pointer because its prologue uses
 * 32-bit mov %ebp, %rsp which zero-extends guest RSP when above 4GB.
 * Instead, stack alignment is ensured by the Windows x64 ABI guarantee
 * (RSP+8 is 16-byte aligned on call) and by the prologue pushes.
 */
#if defined(__i386__)
#define WINE_STUB
#else
#define WINE_STUB __attribute__((ms_abi))
#endif

#if defined(__i386__)
#define KERNEL32_STUB __attribute__((stdcall))
#else
#define KERNEL32_STUB __attribute__((ms_abi))
#endif

/*
 * WINE_STUB_STATIC — same as WINE_STUB but for static/internal functions.
 */
#if defined(__i386__)
#define WINE_STUB_STATIC static
#else
#define WINE_STUB_STATIC static __attribute__((ms_abi))
#endif

/*
 * FORCE_PTR_RETURN(p) — force a pointer return in the correct register.
 *
 * x86_64: GCC with ms_abi may emit 32-bit "mov $imm, %eax" for pointer
 *   returns. Force the full RAX register write.
 * i386:   Just return normally — EAX is 32 bits, which is all we need.
 */
#if defined(__i386__)
#define FORCE_PTR_RETURN(p) ((void *)(uintptr_t)(p))
#else
#define FORCE_PTR_RETURN(p) ({                      \
    uintptr_t _pr = (uintptr_t)(p);                 \
    __asm__ __volatile__("movq %0,%%rax"           \
        : : "r"(_pr) : "rax", "memory");            \
    (void *)(uintptr_t)_pr;                         \
})
#endif

#endif /* WINE_ABI_H */
