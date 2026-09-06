#ifndef HANDLER_ABI_H
#define HANDLER_ABI_H

/* Functions called directly from the PE — use Microsoft x64 ABI */
#if defined(__i386__)
#define WINE_STUB __attribute__((used))
#else
#define WINE_STUB __attribute__((ms_abi, used))
#endif

/* Functions called from C code (Wine stubs) — use System V ABI */
#define HANDLER __attribute__((used))

#endif
