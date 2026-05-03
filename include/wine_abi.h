/*
 * wine_abi.h — ABI attribute macros for functions called from guest PE code.
 *
 * All functions that are invoked from Windows x64 guest binaries must use the
 * Microsoft x64 calling convention (ms_abi) and ensure the stack pointer is
 * 16-byte aligned on entry (force_align_arg_pointer).
 */

#ifndef WINE_ABI_H
#define WINE_ABI_H

/*
 * WINE_STUB — marks a non-static function as using the Microsoft x64 calling
 * convention with stack alignment. Use for all exported stub functions called
 * from guest PE code (kernel32, msvcrt, ntdll exports).
 */
#define WINE_STUB __attribute__((ms_abi, force_align_arg_pointer))

/*
 * WINE_STUB_STATIC — same as WINE_STUB but for static/internal functions.
 */
#define WINE_STUB_STATIC static __attribute__((ms_abi, force_align_arg_pointer))

#endif /* WINE_ABI_H */
