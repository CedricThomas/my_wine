/*
 * ntdll_handle.c — Handle table infrastructure
 *
 * Manages the Windows handle-to-Linux-FD mapping, constructor init,
 * and the NtClose handler.
 */

#include <stdint.h>
#include <unistd.h>
#include "handler_abi.h"
#include "ntdll_priv.h"
#include "../syscall/syscalls_inline.h"

/* ── Handle Table ──────────────────────────────────────────────── */

handle_entry_t handle_table[HANDLE_TABLE_SIZE];

void init_handle_table(void)
{
    handle_table[0].fd   = STDIN_FILENO;
    handle_table[0].used = 1;
    handle_table[1].fd   = STDOUT_FILENO;
    handle_table[1].used = 1;
    handle_table[2].fd   = STDERR_FILENO;
    handle_table[2].used = 1;
}

static __attribute__((constructor)) void ntdll_init(void)
{
    init_handle_table();
}

/* Convert a Windows handle index to its Linux FD.
   Returns -1 on invalid handle. */
int handle_to_fd(uint64_t handle)
{
    if (handle == STDIN_HANDLE)  return handle_table[0].fd;
    if (handle == STDOUT_HANDLE) return handle_table[1].fd;
    if (handle == STDERR_HANDLE) return handle_table[2].fd;

    unsigned idx = (unsigned)handle;
    if (idx >= HANDLE_TABLE_SIZE || !handle_table[idx].used)
        return -1;
    return handle_table[idx].fd;
}

/* Allocate a new Windows handle for a Linux FD.
   Returns the handle value (as uint64_t) or 0 on failure. */
uint64_t fd_to_handle(int fd)
{
    unsigned idx;
    for (idx = 3; idx < HANDLE_TABLE_SIZE; idx++) {
        if (!handle_table[idx].used) {
            handle_table[idx].fd   = fd;
            handle_table[idx].used = 1;
            return (uint64_t)idx;
        }
    }
    return 0;
}

/* Remove a handle from the table. */
void free_handle(uint64_t handle)
{
    if (handle == STDIN_HANDLE || handle == STDOUT_HANDLE || handle == STDERR_HANDLE)
        return;

    unsigned idx = (unsigned)handle;
    if (idx < HANDLE_TABLE_SIZE)
        handle_table[idx].used = 0;
}

/* ── NtClose ───────────────────────────────────────────────────── */

HANDLER
uint64_t handler_NtClose(uint64_t handle)
{
    int fd = handle_to_fd(handle);
    if (fd < 0) return STATUS_INVALID_HANDLE;

    INLINE_SYSCALL_CLOSE(fd);
    free_handle(handle);

    return STATUS_SUCCESS;
}
