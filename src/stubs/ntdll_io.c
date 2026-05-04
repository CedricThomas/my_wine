/*
 * ntdll_io.c — I/O syscall handlers
 *
 * NtWriteFile, NtReadFile, NtOpenFile
 */

#include <stdint.h>
#include <stddef.h>
#include <unistd.h>
#include <asm/unistd_64.h>
#include "handler_abi.h"
#include "ntdll_priv.h"

#define AT_FDCWD ((long)-100)

HANDLER
uint64_t handler_NtWriteFile(uint64_t file_handle, uint64_t event, uint64_t apc,
                             uint64_t context, uint64_t buffer, uint64_t length,
                             uint64_t byte_offset, uint64_t bytes_written)
{
    (void)event; (void)apc; (void)context; (void)byte_offset;

    int fd = handle_to_fd(file_handle);
    if (fd < 0) return STATUS_INVALID_HANDLE;

    const char *buf = (const char *)(uintptr_t)buffer;
    long res;
    __asm__ volatile("syscall" : "=a"(res) : "a"(__NR_write), "D"(fd), "S"(buf), "d"((size_t)length) : "rcx", "r11", "memory", "cc");
    ssize_t n = (ssize_t)res;
    if (n < 0) return STATUS_UNSUCCESSFUL;

    if (bytes_written != 0)
        *(uint64_t *)(uintptr_t)bytes_written = (uint64_t)n;

    return STATUS_SUCCESS;
}

HANDLER
uint64_t handler_NtReadFile(uint64_t file_handle, uint64_t event, uint64_t apc,
                            uint64_t context, uint64_t buffer, uint64_t length,
                            uint64_t byte_offset, uint64_t bytes_read)
{
    (void)event; (void)apc; (void)context; (void)byte_offset;

    int fd = handle_to_fd(file_handle);
    if (fd < 0) return STATUS_INVALID_HANDLE;

    char *buf = (char *)(uintptr_t)buffer;
    long res;
    __asm__ volatile("syscall" : "=a"(res) : "a"(__NR_read), "D"(fd), "S"(buf), "d"((size_t)length) : "rcx", "r11", "memory", "cc");
    ssize_t n = (ssize_t)res;
    if (n < 0) return STATUS_UNSUCCESSFUL;

    if (bytes_read != 0)
        *(uint64_t *)(uintptr_t)bytes_read = (uint64_t)n;

    return STATUS_SUCCESS;
}

HANDLER
uint64_t handler_NtOpenFile(uint64_t *file_handle, uint64_t desired_access,
                            uint64_t object_attributes, uint64_t io_status_block,
                            uint64_t share_access, uint64_t dispose)
{
    (void)share_access;
    (void)dispose;

    const char *path = NULL;
    int oflags = 0;

    /* Map Windows desired_access to Linux open flags */
    uint64_t GENERIC_READ  = 0x80000000;
    uint64_t GENERIC_WRITE = 0x40000000;

    if (desired_access & GENERIC_READ)
        oflags |= 0; /* O_RDONLY */
    if (desired_access & GENERIC_WRITE)
        oflags |= 2; /* O_RDWR */
    if (!(desired_access & GENERIC_READ) && !(desired_access & GENERIC_WRITE))
        oflags = 0; /* O_RDONLY */

    /* Extract path from OBJECT_ATTRIBUTES if provided */
    if (object_attributes != 0) {
        /* Read ObjectName pointer from OBJECT_ATTRIBUTES */
        uint64_t object_name_ptr = ((OBJECT_ATTRIBUTES *)object_attributes)->ObjectName;

        if (object_name_ptr != 0) {
            /* Read UNICODE_STRING */
            uint16_t wcs_len = ((UNICODE_STRING *)object_name_ptr)->Length;
            if (wcs_len > 16384) wcs_len = 16384; /* cap at 8192 wchar_t */
            const wchar_t *wcs = (const wchar_t *)(
                (uintptr_t)((UNICODE_STRING *)object_name_ptr)->Buffer
            );
            if (wcs_len > 0) {
                /* Convert from UTF-16 to UTF-8 (assume ASCII for simplicity) */
                char utf8[2048];
                int max_len = wcs_len / 2;
                if (max_len > 2047) max_len = 2047;
                int i;
                for (i = 0; i < max_len && wcs[i] != 0; i++) {
                    if (wcs[i] < 0x80)
                        utf8[i] = (char)wcs[i];
                    else
                        utf8[i] = '?';
                }
                utf8[i] = '\0';
                path = utf8;
            }
        }
    }

    /* If no path extracted, fall back to /dev/null */
    const char *open_path = path ? path : "/dev/null";

    /* Open the file via openat syscall (avoids libc after GS base change) */
    long res;
    __asm__ volatile("syscall" : "=a"(res) : "a"(__NR_openat), "D"(AT_FDCWD), "S"(open_path), "d"(oflags) : "rcx", "r11", "memory", "cc");
    int fd = (int)res;
    if (fd < 0) {
        return STATUS_UNSUCCESSFUL;
    }

    /* Store in handle table */
    uint64_t handle = fd_to_handle(fd);
    if (handle == 0) {
        __asm__ volatile("syscall" : "=a"(res) : "a"(__NR_close), "D"(fd) : "rcx", "r11", "cc");
        return STATUS_UNSUCCESSFUL;
    }

    /* Write handle back */
    *file_handle = handle;

    /* Write IO_STATUS_BLOCK if provided (Information field at offset 8) */
    if (io_status_block != 0) {
        *(uint64_t *)((uintptr_t)io_status_block + 8) = 0; /* Information = 0 */
    }

    return STATUS_SUCCESS;
}
