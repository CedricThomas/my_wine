#ifndef MY_WINE_KERNEL32_PRIV_H
#define MY_WINE_KERNEL32_PRIV_H

#include <stdint.h>
#include <stddef.h>
#include <time.h>
#include <unistd.h>
#include <sys/mman.h>
#include "include/kernel32.h"
#include "include/nt_constants.h"
#include "include/ntdll.h"
#include "include/syscall/thunk_gen.h"
#include "include/wine_abi.h"
#include "../syscall/abi_wrappers.h"
#include "include/common.h"
#include "ntdll_priv.h"
#include "../syscall/syscalls_inline.h"

/* Shared helper: write a static message to stderr via direct syscall */
KERNEL32_STUB
void write_to_stderr(const char *msg);

/* Thread-local last-error code (defined in kernel32_misc.c) */
extern uint32_t g_last_error;

#endif /* MY_WINE_KERNEL32_PRIV_H */
