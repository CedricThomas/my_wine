#ifndef HANDLER_ABI_H
#define HANDLER_ABI_H

/* Functions called directly from the PE — use Microsoft x64 ABI */
#define WINE_STUB __attribute__((ms_abi, used))

/* Functions called from C code (Wine stubs) — use System V ABI */
#define HANDLER __attribute__((used))

#endif
