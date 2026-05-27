/*
 * kernel32_path.c
 *
 * Generic current-directory and DOS-path compatibility helpers shared by the
 * guest-facing kernel32 path APIs. These routines are intentionally separate
 * from Doom95-specific compatibility code.
 */

#define _GNU_SOURCE

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <dirent.h>
#include <asm/unistd.h>

#include "kernel32_priv.h"
#include "../loader/loader_state.h"

#define FILE_ATTRIBUTE_DIRECTORY 0x10
#define FILE_ATTRIBUTE_NORMAL    0x80

static char g_process_current_directory[1024];

static long wine_path_syscall2(long nr, long a0, long a1)
{
#if defined(__i386__)
    long ret;
    __asm__ volatile("int $0x80"
                     : "=a"(ret)
                     : "a"(nr), "b"(a0), "c"(a1)
                     : "cc", "memory");
    return ret;
#else
    long ret;
    __asm__ volatile("syscall"
                     : "=a"(ret)
                     : "a"(nr), "D"(a0), "S"(a1)
                     : "rcx", "r11", "cc", "memory");
    return ret;
#endif
}

void wine_reset_current_directory_cache(void)
{
    g_process_current_directory[0] = '\0';
}

static void wine_copy_cstr(char *dst, size_t dst_size, const char *src)
{
    size_t len;

    if (!dst || dst_size == 0)
        return;
    if (!src)
        src = "";

    len = strlen(src);
    if (len >= dst_size)
        len = dst_size - 1;
    memcpy(dst, src, len);
    dst[len] = '\0';
}

static void wine_init_process_directory(void)
{
    const char *pe_path;
    const char *slash;
    char cwd[1024];
    size_t len;

    if (g_process_current_directory[0] != '\0')
        return;

    pe_path = g_loader.pe_path;
    if (!pe_path || pe_path[0] == '\0') {
        g_process_current_directory[0] = '.';
        g_process_current_directory[1] = '\0';
        return;
    }

    slash = strrchr(pe_path, '/');
    if (!slash) {
        g_process_current_directory[0] = '.';
        g_process_current_directory[1] = '\0';
        return;
    }

    len = (size_t)(slash - pe_path);
    if (len == 0) {
        g_process_current_directory[0] = '/';
        g_process_current_directory[1] = '\0';
        return;
    }

    if (len >= sizeof(g_process_current_directory))
        len = sizeof(g_process_current_directory) - 1;

    /*
     * Normalize relative launch paths to an absolute host path so later
     * case-insensitive lookups do not depend on the caller's cwd shape.
     */
    if (pe_path[0] != '/' && getcwd(cwd, sizeof(cwd)) != NULL) {
        size_t cwd_len = strlen(cwd);

        if (cwd_len > 0 && cwd[cwd_len - 1] == '/')
            cwd_len--;
        if (cwd_len + 1 + len < sizeof(g_process_current_directory)) {
            memcpy(g_process_current_directory, cwd, cwd_len);
            g_process_current_directory[cwd_len] = '/';
            memcpy(g_process_current_directory + cwd_len + 1, pe_path, len);
            g_process_current_directory[cwd_len + 1 + len] = '\0';
            return;
        }
    }

    memcpy(g_process_current_directory, pe_path, len);
    g_process_current_directory[len] = '\0';
}

const char *wine_get_current_directory(void)
{
    wine_init_process_directory();
    return g_process_current_directory;
}

int wine_resolve_path(const char *src, char *dst, size_t dst_size)
{
    size_t i = 0;

    if (!src || !dst || dst_size < 2)
        return 0;

    wine_init_process_directory();

    if (src[0] != '\0' && src[1] == ':') {
        src += 2;
        if (*src == '\\' || *src == '/')
            src++;
        dst[i++] = '/';
    } else if (*src == '/' || *src == '\\') {
        while (*src == '/' || *src == '\\')
            src++;
        dst[i++] = '/';
    } else {
        const char *cwd = wine_get_current_directory();
        while (*cwd && i + 1 < dst_size)
            dst[i++] = *cwd++;
        if (i == 0 || dst[i - 1] != '/')
            dst[i++] = '/';
    }

    while (*src && i + 1 < dst_size) {
        char ch = (*src == '\\') ? '/' : *src;
        if (ch == '/' && i > 0 && dst[i - 1] == '/') {
            src++;
            continue;
        }
        dst[i++] = ch;
        src++;
    }
    dst[i] = '\0';
    return 1;
}

int wine_set_current_directory(const char *path)
{
    char resolved[1024];
    struct stat st;

    if (!wine_resolve_path(path, resolved, sizeof(resolved)))
        return 0;
    if (stat(resolved, &st) != 0 || !S_ISDIR(st.st_mode))
        return 0;

    wine_copy_cstr(g_process_current_directory, sizeof(g_process_current_directory), resolved);
    return chdir(g_process_current_directory) == 0;
}

static int wine_path_lookup_case_insensitive(const char *path, char *resolved, size_t resolved_size)
{
    char current[1024];
    const char *segment;
    struct stat st;

    if (!path || !resolved || resolved_size == 0)
        return 0;

    if (stat(path, &st) == 0) {
        wine_copy_cstr(resolved, resolved_size, path);
        return 1;
    }

    if (path[0] != '/')
        return 0;

    current[0] = '/';
    current[1] = '\0';
    segment = path + 1;

    while (*segment != '\0') {
        const char *next = segment;
        char wanted[256];
        size_t wanted_len = 0;
        DIR *dir;
        struct dirent *entry;
        const char *match = NULL;
        size_t current_len;

        while (*next != '\0' && *next != '/')
            next++;
        wanted_len = (size_t)(next - segment);
        if (wanted_len == 0) {
            segment = (*next == '/') ? next + 1 : next;
            continue;
        }
        if (wanted_len >= sizeof(wanted))
            return 0;
        memcpy(wanted, segment, wanted_len);
        wanted[wanted_len] = '\0';

        dir = opendir(current);
        if (!dir)
            return 0;

        while ((entry = readdir(dir)) != NULL) {
            if (strcasecmp(entry->d_name, wanted) == 0) {
                match = entry->d_name;
                break;
            }
        }

        if (!match) {
            closedir(dir);
            return 0;
        }

        current_len = strlen(current);
        if (current_len > 1) {
            if (current_len + 1 >= sizeof(current)) {
                closedir(dir);
                return 0;
            }
            current[current_len++] = '/';
            current[current_len] = '\0';
        }
        if (current_len + strlen(match) >= sizeof(current)) {
            closedir(dir);
            return 0;
        }
        memcpy(current + current_len, match, strlen(match) + 1);
        closedir(dir);

        segment = (*next == '/') ? next + 1 : next;
    }

    if (stat(current, &st) != 0)
        return 0;

    wine_copy_cstr(resolved, resolved_size, current);
    return 1;
}

static int wine_build_search_candidate(const char *directory, const char *filename,
                                       const char *extension, char *candidate,
                                       size_t candidate_size)
{
    const char *base = filename ? filename : "";
    int has_extension = 0;
    const char *scan;
    size_t used = 0;

    if (!candidate || candidate_size == 0)
        return 0;

    candidate[0] = '\0';
    if (directory && directory[0] != '\0') {
        used = snprintf(candidate, candidate_size, "%s", directory);
        if (used >= candidate_size)
            return 0;
        if (used > 0 && candidate[used - 1] != '/' && candidate[used - 1] != '\\') {
            if (used + 1 >= candidate_size)
                return 0;
            candidate[used++] = '\\';
            candidate[used] = '\0';
        }
    }

    if (used + strlen(base) >= candidate_size)
        return 0;
    memcpy(candidate + used, base, strlen(base) + 1);

    scan = strrchr(base, '\\');
    if (!scan)
        scan = strrchr(base, '/');
    scan = scan ? scan + 1 : base;
    has_extension = strrchr(scan, '.') != NULL;

    if (!has_extension && extension && extension[0] != '\0') {
        size_t ext_len = strlen(extension);
        if (used + strlen(base) + ext_len >= candidate_size)
            return 0;
        memcpy(candidate + used + strlen(base), extension, ext_len + 1);
    }

    return 1;
}

static int wine_search_existing_path(const char *directory, const char *filename,
                                     const char *extension, char *resolved,
                                     size_t resolved_size)
{
    char candidate[1024];
    char unix_path[1024];

    if (!wine_build_search_candidate(directory, filename, extension, candidate, sizeof(candidate)))
        return 0;
    if (!wine_resolve_path(candidate, unix_path, sizeof(unix_path)))
        return 0;
    return wine_path_lookup_case_insensitive(unix_path, resolved, resolved_size);
}

KERNEL32_STUB
uint32_t GetFileAttributesA(const char *lpFileName)
{
    char path[1024];
    char resolved[1024];
    struct stat st;
    int rc;

    if (!lpFileName || lpFileName[0] == '\0')
        return 0xffffffffu;
    if (strcasestr(lpFileName, "wad"))
        fprintf(stderr, "GetFileAttributesA('%s')\n", lpFileName);

    if (!wine_resolve_path(lpFileName, path, sizeof(path)))
        return 0xffffffffu;

    if (!wine_path_lookup_case_insensitive(path, resolved, sizeof(resolved)))
        return 0xffffffffu;
    if (strcasestr(lpFileName, "wad"))
        fprintf(stderr, "GetFileAttributesA -> '%s'\n", resolved);

    rc = stat(resolved, &st);
    if (rc != 0)
        return 0xffffffffu;
    if (S_ISDIR(st.st_mode))
        return FILE_ATTRIBUTE_DIRECTORY;
    return FILE_ATTRIBUTE_NORMAL;
}

KERNEL32_STUB
int CreateDirectoryA(const char *lpPathName, void *lpSecurityAttributes)
{
    char path[1024];

    (void)lpSecurityAttributes;

    if (!wine_resolve_path(lpPathName, path, sizeof(path)))
        return 0;
    return wine_path_syscall2(__NR_mkdir, (long)path, 0755) == 0;
}

KERNEL32_STUB
uint32_t SearchPathA(const char *lpPath, const char *lpFileName, const char *lpExtension,
                     uint32_t nBufferLength, char *lpBuffer, char **lpFilePart)
{
    char resolved[1024];
    char path_list[1024];
    const char *src = resolved;
    uint32_t len = 0;
    int found = 0;

    if (!lpFileName)
        return 0;
    if (strcasestr(lpFileName, "wad"))
        fprintf(stderr, "SearchPathA(path='%s', file='%s', ext='%s')\n",
                lpPath ? lpPath : "", lpFileName, lpExtension ? lpExtension : "");

    if (strchr(lpFileName, '\\') || strchr(lpFileName, '/') ||
        (lpFileName[0] != '\0' && lpFileName[1] == ':')) {
        found = wine_search_existing_path(NULL, lpFileName, lpExtension, resolved, sizeof(resolved));
    } else {
        const char *segment;

        if (lpPath && lpPath[0] != '\0') {
            wine_copy_cstr(path_list, sizeof(path_list), lpPath);
            segment = path_list;
            while (*segment != '\0' && !found) {
                char *end = (char *)strchr(segment, ';');
                char saved = '\0';

                if (end) {
                    saved = *end;
                    *end = '\0';
                }
                if (segment[0] != '\0')
                    found = wine_search_existing_path(segment, lpFileName, lpExtension,
                                                      resolved, sizeof(resolved));
                if (!end)
                    break;
                *end = saved;
                segment = end + 1;
            }
        }

        if (!found)
            found = wine_search_existing_path(NULL, lpFileName, lpExtension,
                                              resolved, sizeof(resolved));
    }

    if (!found)
        return 0;
    if (strcasestr(lpFileName, "wad"))
        fprintf(stderr, "SearchPathA -> '%s'\n", resolved);

    while (src[len] != '\0')
        len++;

    if (!lpBuffer || nBufferLength == 0)
        return len;

    if (len + 1 > nBufferLength)
        len = nBufferLength - 1;
    memcpy(lpBuffer, src, len);
    lpBuffer[len] = '\0';
    if (lpFilePart) {
        char *slash = strrchr(lpBuffer, '/');
        *lpFilePart = slash ? slash + 1 : lpBuffer;
    }
    return len;
}
