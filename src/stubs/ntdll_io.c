/*
 * ntdll_io.c — I/O syscall handlers
 *
 * NtWriteFile, NtReadFile, NtOpenFile
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>
#include <fcntl.h>

#include "ntdll_priv.h"

uint64_t handler_NtWriteFile(uint64_t file_handle, uint64_t event, uint64_t apc,
                             uint64_t context, uint64_t buffer, uint64_t length,
                             uint64_t byte_offset, uint64_t bytes_written)
{
    (void)event; (void)apc; (void)context; (void)byte_offset;

    int fd = handle_to_fd(file_handle);
    if (fd < 0) return STATUS_INVALID_HANDLE;

    const char *buf = (const char *)(uintptr_t)buffer;
    ssize_t n = write(fd, buf, (size_t)length);
    if (n < 0) return STATUS_UNSUCCESSFUL;

    if (bytes_written != 0)
        *(uint64_t *)(uintptr_t)bytes_written = (uint64_t)n;

    return STATUS_SUCCESS;
}

uint64_t handler_NtReadFile(uint64_t file_handle, uint64_t event, uint64_t apc,
                            uint64_t context, uint64_t buffer, uint64_t length,
                            uint64_t byte_offset, uint64_t bytes_read)
{
    (void)event; (void)apc; (void)context; (void)byte_offset;

    int fd = handle_to_fd(file_handle);
    if (fd < 0) return STATUS_INVALID_HANDLE;

    char *buf = (char *)(uintptr_t)buffer;
    ssize_t n = read(fd, buf, (size_t)length);
    if (n < 0) return STATUS_UNSUCCESSFUL;

    if (bytes_read != 0)
        *(uint64_t *)(uintptr_t)bytes_read = (uint64_t)n;

    return STATUS_SUCCESS;
}

/*
 * handler_NtOpenFile
 *
 * OBJECT_ATTRIBUTES (x64, 40 bytes):
 *   uint32_t  Length           (24)
 *   int32_t   pad
 *   uint64_t  RootDirectory
 *   uint64_t  ObjectName  (pointer to UNICODE_STRING)
 *   uint32_t  Attributes
 *   int32_t   pad
 *   uint64_t  SecurityDescriptor
 *   uint64_t  SecurityQualityOfService
 *
 * UNICODE_STRING (x64, 12 bytes):
 *   uint16_t  Length
 *   uint16_t  MaximumLength
 *   uint64_t  Buffer     (pointer to wchar_t string)
 */
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
        oflags |= O_RDONLY;
    if (desired_access & GENERIC_WRITE)
        oflags |= O_RDWR;
    if (!(desired_access & GENERIC_READ) && !(desired_access & GENERIC_WRITE))
        oflags = O_RDONLY; /* default */

    /* Extract path from OBJECT_ATTRIBUTES if provided */
    if (object_attributes != 0) {
        /* Read ObjectName pointer (offset 16 in OBJECT_ATTRIBUTES) */
        uint64_t object_name_ptr = *(uint64_t *)((uintptr_t)object_attributes + 16);

        if (object_name_ptr != 0) {
            /* Read UNICODE_STRING (12 bytes) */
            uint16_t wcs_len = *(uint16_t *)(uintptr_t)object_name_ptr;
            const wchar_t *wcs = (const wchar_t *)(
                (uintptr_t)object_name_ptr + 12
            );
            if (wcs_len > 0) {
                /* Convert from UTF-16 to UTF-8 (assume ASCII for simplicity) */
                int max_len = wcs_len / 2;
                /* Allocate on heap for safety */
                char *utf8 = malloc((size_t)max_len + 1);
                if (utf8) {
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
    }

    /* If no path extracted, fall back to /dev/null */
    const char *open_path = path ? path : "/dev/null";

    /* Open the file */
    int fd = open(open_path, oflags);
    if (fd < 0) {
        free((void *)path);
        return STATUS_UNSUCCESSFUL;
    }

    /* Store in handle table */
    uint64_t handle = fd_to_handle(fd);
    /* Free the malloc'd path buffer on all paths after open() succeeded.
     * Placed before the handle check so it runs on both success and failure. */
    free((void *)path);
    if (handle == 0) {
        close(fd);
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
