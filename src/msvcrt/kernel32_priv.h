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
#include "include/syscall_safe_utils.h"

/* Shared helper: write a static message to stderr via direct syscall */
KERNEL32_STUB
void write_to_stderr(const char *msg);

/* Thread-local last-error code (defined in kernel32_misc.c) */
extern uint32_t g_last_error;

typedef struct {
    uint32_t dwFileAttributes;
    uint32_t ftCreationTimeLow;
    uint32_t ftCreationTimeHigh;
    uint32_t ftLastAccessTimeLow;
    uint32_t ftLastAccessTimeHigh;
    uint32_t ftLastWriteTimeLow;
    uint32_t ftLastWriteTimeHigh;
    uint32_t nFileSizeHigh;
    uint32_t nFileSizeLow;
    uint32_t dwReserved0;
    uint32_t dwReserved1;
    char cFileName[260];
    char cAlternateFileName[14];
} WIN32_FIND_DATAA_WINE;

typedef struct {
    void *dir;
    char directory[1024];
    char pattern[260];
    int exact_done;
} wine_find_handle;

int wine_resolve_path(const char *src, char *dst, size_t dst_size);
const char *wine_get_current_directory(void);
int wine_set_current_directory(const char *path);

#endif /* MY_WINE_KERNEL32_PRIV_H */
