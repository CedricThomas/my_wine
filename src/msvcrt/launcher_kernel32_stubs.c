#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/stat.h>
#include <dirent.h>

#include "kernel32_priv.h"
#include "include/handle_manager.h"

extern uint32_t g_last_error;

typedef struct {
    uint16_t wYear;
    uint16_t wMonth;
    uint16_t wDayOfWeek;
    uint16_t wDay;
    uint16_t wHour;
    uint16_t wMinute;
    uint16_t wSecond;
    uint16_t wMilliseconds;
} SYSTEMTIME_WINE;

#define FILE_ATTRIBUTE_DIRECTORY 0x10
#define FILE_ATTRIBUTE_NORMAL    0x80

static void fill_find_data(const char *resolved_path, const struct stat *st,
                           WIN32_FIND_DATAA_WINE *data)
{
    const char *name;

    memset(data, 0, sizeof(*data));
    data->dwFileAttributes = S_ISDIR(st->st_mode) ?
        FILE_ATTRIBUTE_DIRECTORY : FILE_ATTRIBUTE_NORMAL;
    data->nFileSizeLow = (uint32_t)st->st_size;
    data->nFileSizeHigh = (uint32_t)(((uint64_t)st->st_size) >> 32);
    name = strrchr(resolved_path, '/');
    name = name ? name + 1 : resolved_path;
    snprintf(data->cFileName, sizeof(data->cFileName), "%s", name);
}

static int wildcard_match_ci(const char *pattern, const char *text)
{
    if (*pattern == '\0')
        return *text == '\0';
    if (*pattern == '*') {
        pattern++;
        if (*pattern == '\0')
            return 1;
        while (*text != '\0') {
            if (wildcard_match_ci(pattern, text))
                return 1;
            text++;
        }
        return wildcard_match_ci(pattern, text);
    }
    if (*pattern == '?')
        return *text != '\0' && wildcard_match_ci(pattern + 1, text + 1);
    if (((unsigned char)*pattern | 32) != ((unsigned char)*text | 32))
        return 0;
    return wildcard_match_ci(pattern + 1, text + 1);
}

static int find_handle_next_match(wine_find_handle *find, WIN32_FIND_DATAA_WINE *data)
{
    DIR *dir = (DIR *)find->dir;
    struct dirent *entry;
    char full_path[1400];
    struct stat st;

    if (dir == NULL)
        return 0;

    while ((entry = readdir(dir)) != NULL) {
        if (!wildcard_match_ci(find->pattern, entry->d_name))
            continue;
        snprintf(full_path, sizeof(full_path), "%s/%s", find->directory, entry->d_name);
        if (stat(full_path, &st) != 0)
            continue;
        fill_find_data(full_path, &st, data);
        return 1;
    }

    return 0;
}

KERNEL32_STUB void DebugBreak(void) {}

KERNEL32_STUB uint32_t SetErrorMode(uint32_t mode)
{
    (void)mode;
    return 0;
}

KERNEL32_STUB BOOL SetCurrentDirectoryA(const char *path)
{
    return wine_set_current_directory(path) ? TRUE : FALSE;
}

KERNEL32_STUB BOOL SetEnvironmentVariableA(const char *name, const char *value)
{
    (void)name;
    (void)value;
    return TRUE;
}

KERNEL32_STUB void *GetEnvironmentStringsW(void)
{
    static uint16_t empty_env_block[2] = { 0, 0 };
    return FORCE_PTR_RETURN(empty_env_block);
}

KERNEL32_STUB BOOL FreeEnvironmentStringsA(void *env)
{
    (void)env;
    return TRUE;
}

KERNEL32_STUB BOOL FreeEnvironmentStringsW(void *env)
{
    (void)env;
    return TRUE;
}

KERNEL32_STUB uint32_t GetOEMCP(void)
{
    return 437;
}

KERNEL32_STUB uint32_t GetACP(void)
{
    return 1252;
}

KERNEL32_STUB uint32_t GetCurrentDirectoryA(uint32_t nBufferLength, char *lpBuffer)
{
    const char *cwd = wine_get_current_directory();
    uint32_t len = (uint32_t)strlen(cwd);

    if (lpBuffer && nBufferLength != 0) {
        uint32_t copy_len = len;
        if (copy_len + 1 > nBufferLength)
            copy_len = nBufferLength - 1;
        memcpy(lpBuffer, cwd, copy_len);
        lpBuffer[copy_len] = '\0';
    }

    return len;
}

KERNEL32_STUB void SetLastError(uint32_t err)
{
    g_last_error = err;
}

KERNEL32_STUB int CompareStringA(uint32_t locale, uint32_t flags, const char *a, int a_len,
                                 const char *b, int b_len)
{
    (void)locale;
    (void)flags;
    (void)a_len;
    (void)b_len;
    if (a == NULL || b == NULL)
        return 0;
    while (*a != '\0' && *b != '\0' && *a == *b) {
        a++;
        b++;
    }
    if (*a == *b)
        return 2;
    return (*a < *b) ? 1 : 3;
}

KERNEL32_STUB int CompareStringW(uint32_t locale, uint32_t flags, const uint16_t *a, int a_len,
                                 const uint16_t *b, int b_len)
{
    (void)locale;
    (void)flags;
    (void)a_len;
    (void)b_len;
    if (a == NULL || b == NULL)
        return 0;
    while (*a != 0 && *b != 0 && *a == *b) {
        a++;
        b++;
    }
    if (*a == *b)
        return 2;
    return (*a < *b) ? 1 : 3;
}

KERNEL32_STUB int LCMapStringA(uint32_t locale, uint32_t flags, const char *src, int src_len,
                               char *dst, int dst_len)
{
    int i;

    (void)locale;
    (void)flags;
    if (src == NULL)
        return 0;
    if (src_len < 0) {
        src_len = 0;
        while (src[src_len] != '\0')
            src_len++;
        src_len++;
    }
    if (dst == NULL || dst_len == 0)
        return src_len;
    for (i = 0; i + 1 < dst_len && i < src_len && src[i] != '\0'; i++)
        dst[i] = src[i];
    dst[i] = '\0';
    return i + 1;
}

KERNEL32_STUB int LCMapStringW(uint32_t locale, uint32_t flags, const uint16_t *src, int src_len,
                               uint16_t *dst, int dst_len)
{
    int i;

    (void)locale;
    (void)flags;
    if (src == NULL)
        return 0;
    if (src_len < 0) {
        src_len = 0;
        while (src[src_len] != 0)
            src_len++;
        src_len++;
    }
    if (dst == NULL || dst_len == 0)
        return src_len;
    for (i = 0; i + 1 < dst_len && i < src_len && src[i] != 0; i++)
        dst[i] = src[i];
    dst[i] = 0;
    return i + 1;
}

KERNEL32_STUB BOOL GetStringTypeA(uint32_t locale, uint32_t type, const char *src, int count, uint16_t *chartype)
{
    int i;

    (void)locale;
    (void)type;
    if (src == NULL || chartype == NULL || count <= 0)
        return FALSE;
    for (i = 0; i < count; i++)
        chartype[i] = 0;
    return TRUE;
}

KERNEL32_STUB BOOL GetStringTypeW(uint32_t type, const uint16_t *src, int count, uint16_t *chartype)
{
    int i;

    (void)type;
    if (src == NULL || chartype == NULL || count <= 0)
        return FALSE;
    for (i = 0; i < count; i++)
        chartype[i] = 0;
    return TRUE;
}

KERNEL32_STUB void *FindFirstFileA(const char *lpFileName, void *lpFindFileData)
{
    char resolved[1024];
    char *slash;
    struct stat st;
    DIR *dir;
    wine_find_handle *find;
    uint32_t handle;

    if (!lpFileName || !lpFindFileData)
        return FORCE_PTR_RETURN((void *)(uintptr_t)-1);
    if (lpFileName && strcasestr(lpFileName, "wad"))
        fprintf(stderr, "FindFirstFileA('%s')\n", lpFileName);
    if (!wine_resolve_path(lpFileName, resolved, sizeof(resolved)))
        return FORCE_PTR_RETURN((void *)(uintptr_t)-1);

    find = calloc(1, sizeof(*find));
    if (find == NULL)
        return FORCE_PTR_RETURN((void *)(uintptr_t)-1);

    if (strchr(lpFileName, '*') != NULL || strchr(lpFileName, '?') != NULL) {
        slash = strrchr(resolved, '/');
        if (slash == NULL) {
            free(find);
            return FORCE_PTR_RETURN((void *)(uintptr_t)-1);
        }
        *slash = '\0';
        snprintf(find->directory, sizeof(find->directory), "%s", resolved);
        snprintf(find->pattern, sizeof(find->pattern), "%s", slash + 1);
        dir = opendir(find->directory);
        if (dir == NULL) {
            free(find);
            return FORCE_PTR_RETURN((void *)(uintptr_t)-1);
        }
        find->dir = dir;
        if (!find_handle_next_match(find, (WIN32_FIND_DATAA_WINE *)lpFindFileData)) {
            closedir(dir);
            free(find);
            return FORCE_PTR_RETURN((void *)(uintptr_t)-1);
        }
        if (lpFileName && strcasestr(lpFileName, "wad")) {
            WIN32_FIND_DATAA_WINE *data = (WIN32_FIND_DATAA_WINE *)lpFindFileData;
            fprintf(stderr, "FindFirstFileA -> '%s'\n", data->cFileName);
        }
        handle = (uint32_t)wine_handle_alloc(HANDLE_TYPE_HGLOBAL, find);
        if (handle == 0) {
            closedir(dir);
            free(find);
            return FORCE_PTR_RETURN((void *)(uintptr_t)-1);
        }
        return FORCE_PTR_RETURN((void *)(uintptr_t)handle);
    }

    if (stat(resolved, &st) != 0)
    {
        free(find);
        return FORCE_PTR_RETURN((void *)(uintptr_t)-1);
    }

    fill_find_data(resolved, &st, (WIN32_FIND_DATAA_WINE *)lpFindFileData);
    if (lpFileName && strcasestr(lpFileName, "wad")) {
        WIN32_FIND_DATAA_WINE *data = (WIN32_FIND_DATAA_WINE *)lpFindFileData;
        fprintf(stderr, "FindFirstFileA -> '%s'\n", data->cFileName);
    }
    snprintf(find->directory, sizeof(find->directory), "%s", resolved);
    find->exact_done = 1;
    handle = (uint32_t)wine_handle_alloc(HANDLE_TYPE_HGLOBAL, find);
    if (handle == 0)
    {
        free(find);
        return FORCE_PTR_RETURN((void *)(uintptr_t)-1);
    }
    return FORCE_PTR_RETURN((void *)(uintptr_t)handle);
}

KERNEL32_STUB BOOL FindClose(void *hFindFile)
{
    uint32_t handle = (uint32_t)(uintptr_t)hFindFile;
    void *obj;

    if (wine_handle_get_type(handle) != HANDLE_TYPE_HGLOBAL)
        return FALSE;
    obj = wine_handle_get(handle);
    if (obj != NULL) {
        wine_find_handle *find = (wine_find_handle *)obj;
        if (find->dir != NULL)
            closedir((DIR *)find->dir);
        free(obj);
    }
    wine_handle_free(handle);
    return TRUE;
}

KERNEL32_STUB uint32_t SetHandleCount(uint32_t count)
{
    return count;
}

KERNEL32_STUB BOOL FlushFileBuffers(void *hFile)
{
    int fd = handle_to_fd((uint64_t)(uintptr_t)hFile);
    if (fd < 0)
        return FALSE;
    return fsync(fd) == 0 ? TRUE : FALSE;
}

KERNEL32_STUB BOOL SetEndOfFile(void *hFile)
{
    int fd = handle_to_fd((uint64_t)(uintptr_t)hFile);
    long pos;

    if (fd < 0)
        return FALSE;
    pos = lseek(fd, 0, SEEK_CUR);
    if (pos < 0)
        return FALSE;
    return ftruncate(fd, pos) == 0 ? TRUE : FALSE;
}

KERNEL32_STUB BOOL FileTimeToSystemTime(const FILETIME *file_time, SYSTEMTIME_WINE *system_time)
{
    uint64_t value;
    time_t unix_sec;
    struct tm tm;

    if (!file_time || !system_time)
        return FALSE;

    value = ((uint64_t)file_time->dwHighDateTime << 32) | file_time->dwLowDateTime;
    if (value < 116444736000000000ULL)
        return FALSE;

    unix_sec = (time_t)((value - 116444736000000000ULL) / 10000000ULL);
    if (gmtime_r(&unix_sec, &tm) == NULL)
        return FALSE;

    memset(system_time, 0, sizeof(*system_time));
    system_time->wYear = (uint16_t)(tm.tm_year + 1900);
    system_time->wMonth = (uint16_t)(tm.tm_mon + 1);
    system_time->wDay = (uint16_t)tm.tm_mday;
    system_time->wDayOfWeek = (uint16_t)tm.tm_wday;
    system_time->wHour = (uint16_t)tm.tm_hour;
    system_time->wMinute = (uint16_t)tm.tm_min;
    system_time->wSecond = (uint16_t)tm.tm_sec;
    return TRUE;
}

KERNEL32_STUB BOOL IsBadCodePtr(void *ptr)
{
    (void)ptr;
    return FALSE;
}

KERNEL32_STUB BOOL IsBadReadPtr(const void *ptr, uintptr_t size)
{
    (void)ptr;
    (void)size;
    return FALSE;
}

KERNEL32_STUB BOOL IsBadWritePtr(void *ptr, uintptr_t size)
{
    (void)ptr;
    (void)size;
    return FALSE;
}

KERNEL32_STUB void RtlUnwind(void) {}
