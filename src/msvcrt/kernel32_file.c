#define _GNU_SOURCE

#include <dirent.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

#include "kernel32_priv.h"
#include "msvcrt_priv.h"

#define AT_FDCWD ((long)-100)
#define FILE_ATTRIBUTE_DIRECTORY 0x10
#define FILE_ATTRIBUTE_NORMAL    0x80
#define FILE_TYPE_DISK           1
#define FILE_TYPE_CHAR           2
#define FILE_BEGIN               0
#define FILE_CURRENT             1
#define FILE_END                 2

/* 32-bit CreateFileA fallback handle storage. */
#if defined(__i386__)
int createfile_fds_32[64] = {0};
int createfile_fd_count_32 = 0;
#endif

static long wine_syscall3(long nr, long a0, long a1, long a2)
{
#if defined(__i386__)
    long ret;
    __asm__ volatile("int $0x80"
                     : "=a"(ret)
                     : "a"(nr), "b"(a0), "c"(a1), "d"(a2)
                     : "cc", "memory");
    return ret;
#else
    long ret;
    __asm__ volatile("syscall"
                     : "=a"(ret)
                     : "a"(nr), "D"(a0), "S"(a1), "d"(a2)
                     : "rcx", "r11", "cc", "memory");
    return ret;
#endif
}

static void wine_unix_time_to_filetime(int64_t sec, int64_t nsec, FILETIME *out_ft)
{
    uint64_t value;

    if (!out_ft)
        return;

    value = (uint64_t)sec * 10000000ULL + (uint64_t)(nsec / 100);
    value += 116444736000000000ULL;
    out_ft->dwLowDateTime = (uint32_t)value;
    out_ft->dwHighDateTime = (uint32_t)(value >> 32);
}

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

KERNEL32_STUB
int DeviceIoControl(void *hDevice, uint32_t dwIoControlCode, void *lpInBuffer,
                    uint32_t nInBufferSize, void *lpOutBuffer, uint32_t nOutBufferSize,
                    uint32_t *lpBytesReturned, void *lpOverlapped)
{
    (void)hDevice;
    (void)dwIoControlCode;
    (void)lpInBuffer;
    (void)nInBufferSize;
    (void)lpOutBuffer;
    (void)nOutBufferSize;
    (void)lpOverlapped;
    if (lpBytesReturned)
        *lpBytesReturned = 0;
    return 0;
}

KERNEL32_STUB
uint32_t GetFileType(void *hFile)
{
    uint64_t handle = (uint64_t)(uintptr_t)hFile;

    if (handle == STDIN_HANDLE || handle == STDOUT_HANDLE || handle == STDERR_HANDLE || handle <= 2)
        return FILE_TYPE_CHAR;
    return FILE_TYPE_DISK;
}

KERNEL32_STUB
uint32_t GetFileSize(void *hFile, uint32_t *lpFileSizeHigh)
{
    struct stat st;
    int fd = handle_to_fd((uint64_t)(uintptr_t)hFile);

    if (fd < 0 || INLINE_SYSCALL_FSTAT(fd, &st) != 0)
        return 0xffffffffu;

    if (lpFileSizeHigh)
        *lpFileSizeHigh = (uint32_t)(((uint64_t)st.st_size) >> 32);
    return (uint32_t)st.st_size;
}

KERNEL32_STUB
int GetFileTime(void *hFile, FILETIME *creation, FILETIME *access, FILETIME *write)
{
    struct stat st;
    int fd = handle_to_fd((uint64_t)(uintptr_t)hFile);

    if (fd < 0 || INLINE_SYSCALL_FSTAT(fd, &st) != 0)
        return 0;

    if (creation)
        wine_unix_time_to_filetime(st.st_ctim.tv_sec, st.st_ctim.tv_nsec, creation);
    if (access)
        wine_unix_time_to_filetime(st.st_atim.tv_sec, st.st_atim.tv_nsec, access);
    if (write)
        wine_unix_time_to_filetime(st.st_mtim.tv_sec, st.st_mtim.tv_nsec, write);
    return 1;
}

KERNEL32_STUB
uint32_t SetFilePointer(void *hFile, int32_t lDistanceToMove, int32_t *lpDistanceToMoveHigh,
                        uint32_t dwMoveMethod)
{
    int fd = handle_to_fd((uint64_t)(uintptr_t)hFile);
    int whence = SEEK_SET;
    long long distance = (uint32_t)lDistanceToMove;
    long long pos;

    if (lpDistanceToMoveHigh)
        distance |= ((long long)*lpDistanceToMoveHigh) << 32;

    if (fd < 0)
        return 0xffffffffu;

    if (dwMoveMethod == FILE_CURRENT)
        whence = SEEK_CUR;
    else if (dwMoveMethod == FILE_END)
        whence = SEEK_END;

    pos = wine_syscall3(__NR_lseek, fd, (long)distance, whence);
    if (pos < 0)
        return 0xffffffffu;

    if (lpDistanceToMoveHigh)
        *lpDistanceToMoveHigh = (int32_t)(((uint64_t)pos) >> 32);
    return (uint32_t)pos;
}

KERNEL32_STUB
int FindNextFileA(void *hFindFile, void *lpFindFileData)
{
    wine_find_handle *find;
    DIR *dir;
    struct dirent *entry;
    char full_path[1400];
    struct stat st;
    uint32_t handle = (uint32_t)(uintptr_t)hFindFile;
    WIN32_FIND_DATAA_WINE *data = (WIN32_FIND_DATAA_WINE *)lpFindFileData;

    if (wine_handle_get_type(handle) != HANDLE_TYPE_HGLOBAL || data == NULL)
        return 0;

    find = (wine_find_handle *)wine_handle_get(handle);
    if (find == NULL || find->dir == NULL)
        return 0;

    dir = (DIR *)find->dir;
    while ((entry = readdir(dir)) != NULL) {
        const char *pattern = find->pattern;
        const char *text = entry->d_name;
        int star = 0;
        int matched = 1;

        while (*pattern != '\0') {
            if (*pattern == '*') {
                star = 1;
                pattern++;
                if (*pattern == '\0') {
                    matched = 1;
                    break;
                }
                while (*text != '\0' &&
                       (((unsigned char)*pattern | 32) != ((unsigned char)*text | 32))) {
                    text++;
                }
                continue;
            }
            if (*text == '\0') {
                matched = 0;
                break;
            }
            if (*pattern != '?' &&
                (((unsigned char)*pattern | 32) != ((unsigned char)*text | 32))) {
                matched = 0;
                break;
            }
            pattern++;
            text++;
        }
        if (!matched || (!star && *text != '\0'))
            continue;
        snprintf(full_path, sizeof(full_path), "%s/%s", find->directory, entry->d_name);
        if (stat(full_path, &st) != 0)
            continue;
        memset(data, 0, sizeof(*data));
        data->dwFileAttributes = S_ISDIR(st.st_mode) ?
            FILE_ATTRIBUTE_DIRECTORY : FILE_ATTRIBUTE_NORMAL;
        data->nFileSizeLow = (uint32_t)st.st_size;
        data->nFileSizeHigh = (uint32_t)(((uint64_t)st.st_size) >> 32);
        strncpy(data->cFileName, entry->d_name, sizeof(data->cFileName) - 1);
        if (strcasestr(find->pattern, "wad"))
            fprintf(stderr, "FindNextFileA('%s') -> '%s'\n", find->pattern, data->cFileName);
        return 1;
    }

    return 0;
}
