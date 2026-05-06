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
 * WINE_STUB — marks a non-static function as using the Microsoft x64 calling
 * convention. Use for all exported stub functions called from guest PE code
 * (kernel32, msvcrt, ntdll exports).
 *
 * NOTE: We cannot use force_align_arg_pointer because its prologue uses
 * 32-bit mov %ebp, %rsp which zero-extends guest RSP when above 4GB.
 * Instead, stack alignment is ensured by the Windows x64 ABI guarantee
 * (RSP+8 is 16-byte aligned on call) and by the prologue pushes.
 */
#define WINE_STUB __attribute__((ms_abi))

/*
 * WINE_STUB_STATIC — same as WINE_STUB but for static/internal functions.
 */
#define WINE_STUB_STATIC static __attribute__((ms_abi))

/*
 * FORCE_PTR_RETURN(p) — force a 64-bit pointer return in ms_abi functions.
 *
 * GCC with ms_abi may emit 32-bit "mov $imm, %eax" for pointer returns,
 * which zero-extends to RAX. If the pointer has upper bits (which won't for
 * our <4GB mappings) but more importantly this ensures the return value
 * is written to the full RAX register. Use for all pointer-returning stubs.
 */
#define FORCE_PTR_RETURN(p) ({                      \
    uintptr_t _pr = (uintptr_t)(p);                 \
    __asm__ __volatile__("movq %0,%%rax"           \
        : : "r"(_pr) : "rax", "memory");            \
    (void *)(uintptr_t)_pr;                         \
})

#endif /* WINE_ABI_H */
