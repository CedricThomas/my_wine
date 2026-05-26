#define _GNU_SOURCE

#include "kernel32_priv.h"
#include "msvcrt_priv.h"

#define AT_FDCWD ((long)-100)

/* 32-bit CreateFileA fallback handle storage. */
#if defined(__i386__)
int createfile_fds_32[64] = {0};
int createfile_fd_count_32 = 0;
#endif

KERNEL32_STUB
int CloseHandle(void *hObject)
{
    uint64_t handle = (uint64_t)(uintptr_t)hObject;

    if (handle == STDIN_HANDLE || handle == STDOUT_HANDLE || handle == STDERR_HANDLE)
        return 1;

    if (handle <= 2)
        return 1;

#if defined(__i386__)
    {
        int idx = (int)handle - 3;
        if (idx >= 0 && idx < createfile_fd_count_32) {
            int fd = createfile_fds_32[idx];
            if (fd >= 0) {
                INLINE_SYSCALL_CLOSE(fd);
                createfile_fds_32[idx] = -1;
                return 1;
            }
        }
    }
#endif

    return handler_NtClose(handle) == STATUS_SUCCESS;
}

KERNEL32_STUB
void *CreateFileA(const char *lpFileName, uint32_t dwDesiredAccess,
                  uint32_t dwShareMode, void *lpSecurityAttributes,
                  uint32_t dwCreationDisposition, uint32_t dwFlagsAndAttributes,
                  void *hTemplateFile)
{
    (void)dwShareMode;
    (void)lpSecurityAttributes;
    (void)dwFlagsAndAttributes;
    (void)hTemplateFile;

    if (lpFileName == NULL)
        return FORCE_PTR_RETURN(INVALID_HANDLE_VALUE);

    char path_buf[1024];
    if (!wine_resolve_path(lpFileName, path_buf, sizeof(path_buf)))
        return FORCE_PTR_RETURN(INVALID_HANDLE_VALUE);

    int oflags = 0;
    uint64_t desired = (uint64_t)dwDesiredAccess;
    if (desired & GENERIC_WRITE)
        oflags = 2;

    switch (dwCreationDisposition) {
    case 1: oflags |= 0200 | 0100; break;
    case 2: oflags |= 01000 | 0100; break;
    case 3: break;
    case 4: oflags |= 0100; break;
    case 5: oflags |= 01000; break;
    default: break;
    }

    {
        long res = INLINE_SYSCALL_OPENAT(AT_FDCWD, path_buf, oflags, 0644);
        int fd = (int)res;
        uint64_t handle;
        if (fd < 0)
            return FORCE_PTR_RETURN(INVALID_HANDLE_VALUE);

#if defined(__i386__)
        {
            int idx = createfile_fd_count_32;
            if (idx >= 64) {
                INLINE_SYSCALL_CLOSE(fd);
                return FORCE_PTR_RETURN(INVALID_HANDLE_VALUE);
            }
            createfile_fds_32[idx] = fd;
            handle = (uint64_t)(3 + idx);
            createfile_fd_count_32++;
        }
#else
        handle = fd_to_handle(fd);
#endif

        if (handle == 0) {
            INLINE_SYSCALL_CLOSE(fd);
            return FORCE_PTR_RETURN(INVALID_HANDLE_VALUE);
        }

        return FORCE_PTR_RETURN((void *)(uintptr_t)handle);
    }
}

KERNEL32_STUB
int DeleteFileA(const char *lpFileName)
{
    if (lpFileName == NULL)
        return 0;

    {
        char path_buf[1024];
        long res;
        if (!wine_resolve_path(lpFileName, path_buf, sizeof(path_buf)))
            return 0;

        res = INLINE_SYSCALL_UNLINKAT(AT_FDCWD, path_buf, 0);
        if (res != 0) {
            g_last_error = ERROR_FILE_NOT_FOUND;
            return 0;
        }
    }

    return 1;
}
