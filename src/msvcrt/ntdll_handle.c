/*
 * ntdll_handle.c — Handle table infrastructure
 *
 * Manages the Windows handle-to-Linux-FD mapping and the NtClose handler.
 * Handle allocation/delegation is handled by the handle_manager API.
 */

#define _GNU_SOURCE

#include <stdint.h>
#include <unistd.h>
#include <sys/mman.h>
#include "handler_abi.h"
#include "ntdll_priv.h"
#include "../syscall/syscalls_inline.h"

/* Convert a Windows handle index to its Linux FD.
   Returns -1 on invalid handle. */
int handle_to_fd(uint64_t handle)
{
    if (handle == STDIN_HANDLE)  return STDIN_FILENO;
    if (handle == STDOUT_HANDLE) return STDOUT_FILENO;
    if (handle == STDERR_HANDLE) return STDERR_FILENO;

#if defined(__i386__)
    /* 32-bit: handles 3-64 are direct FD mappings from CreateFileA */
    if (handle >= 3 && handle < 67) {
        extern int createfile_fds_32[];
        extern int createfile_fd_count_32;
        int idx = (int)handle - 3;
        if (idx >= 0 && idx < createfile_fd_count_32)
            return createfile_fds_32[idx];
    }
#endif

    uint8_t type = wine_handle_get_type((uint32_t)handle);
    if (type != HANDLE_TYPE_FILE) return -1;

    void *obj = wine_handle_get((uint32_t)handle);
    if (obj == NULL) return -1;
    return (int)(uintptr_t)obj;
}

/* Allocate a new Windows handle for a Linux FD.
   Returns the handle value (as uint64_t) or 0 on failure. */
uint64_t fd_to_handle(int fd)
{
    return wine_handle_alloc(HANDLE_TYPE_FILE, (void *)(uintptr_t)fd);
}

/* Remove a handle from the table. */
void free_handle(uint64_t handle)
{
    if (handle == STDIN_HANDLE || handle == STDOUT_HANDLE || handle == STDERR_HANDLE)
        return;
    wine_handle_free((uint32_t)handle);
}

/* ── NtClose ───────────────────────────────────────────────────── */

HANDLER
uint64_t handler_NtClose(uint64_t handle)
{
    if (handle == STDIN_HANDLE || handle == STDOUT_HANDLE || handle == STDERR_HANDLE)
        return STATUS_SUCCESS;

    /* stdin/stdout/stderr standard handles (0,1,2) are never closable */
    if (handle <= 2)
        return STATUS_SUCCESS;

    uint8_t type = wine_handle_get_type((uint32_t)handle);
    if (type == 0) {
        /* Not a valid handle in the manager */
        return STATUS_INVALID_HANDLE;
    }

    if (type == HANDLE_TYPE_FILE) {
        void *obj = wine_handle_get((uint32_t)handle);
        if (obj != NULL) {
            INLINE_SYSCALL_CLOSE((int)(uintptr_t)obj);
        }
        wine_handle_free((uint32_t)handle);
        return STATUS_SUCCESS;
    }

    if (type == HANDLE_TYPE_SEMAPHORE) {
        /* No POSIX semaphore to destroy — plain int-based counter */
        wine_handle_free((uint32_t)handle);
        return STATUS_SUCCESS;
    }

    if (type == HANDLE_TYPE_MUTEX) {
        /* No POSIX mutex to destroy — plain int-based lock flag */
        wine_handle_free((uint32_t)handle);
        return STATUS_SUCCESS;
    }

    if (type == HANDLE_TYPE_EVENT) {
        /* No POSIX condvar to destroy — spin-sleep polling only */
        wine_handle_free((uint32_t)handle);
        return STATUS_SUCCESS;
    }

    if (type == HANDLE_TYPE_SECTION) {
        wine_section_t *sec = (wine_section_t *)wine_handle_get((uint32_t)handle);
        if (sec != NULL && sec->base != NULL) {
            INLINE_SYSCALL_MUNMAP(sec->base, sec->size);
            sec->base = NULL;
        }
        wine_handle_free((uint32_t)handle);
        return STATUS_SUCCESS;
    }

    // For other handle types, just free the handle entry
    wine_handle_free((uint32_t)handle);
    return STATUS_SUCCESS;
}
