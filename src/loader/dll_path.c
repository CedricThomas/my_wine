/*
 * dll_path.c — DLL path resolution
 *
 * Searches for DLLs in current directory, app directory, and WINE_DLL_PATH.
 * Extracted from import_resolve.c.
 */

#include "include/common.h"
#include "include/syscall_safe_utils.h"
#include "image_mapper.h"
#include "dll_path.h"
#include "../syscall/syscalls_inline.h"

static char g_exe_dir[512] = {0};

static void dll_ascii_upper_copy(char *dst, size_t dst_size, const char *src)
{
    size_t i = 0;

    if (dst == NULL || dst_size == 0) {
        return;
    }

    if (src == NULL) {
        dst[0] = '\0';
        return;
    }

    while (src[i] != '\0' && i + 1 < dst_size) {
        char ch = src[i];
        if (ch >= 'a' && ch <= 'z')
            ch = (char)(ch - ('a' - 'A'));
        dst[i++] = ch;
    }
    dst[i] = '\0';
}

static int dll_name_has_extension(const char *dll_name)
{
    const char *p;

    if (dll_name == NULL)
        return 0;

    for (p = dll_name; *p != '\0'; p++) {
        if (*p == '.')
            return 1;
    }

    return 0;
}

static int dll_name_has_path_separator(const char *dll_name)
{
    if (dll_name == NULL)
        return 0;
    return syscall_safe_strchr(dll_name, '/') != NULL ||
           syscall_safe_strchr(dll_name, '\\') != NULL;
}

static int try_dll_explicit_path(const char *dll_name, char *path, size_t path_size)
{
    char candidate[512];
    char upper_candidate[512];
    size_t len;
    const char *base_name;

    if (dll_name == NULL)
        return 0;

    syscall_safe_copy_str(candidate, dll_name, sizeof(candidate));
    if (syscall_safe_path_exists(candidate)) {
        syscall_safe_copy_str(path, candidate, path_size);
        return 1;
    }
    syscall_safe_copy_str(upper_candidate, candidate, sizeof(upper_candidate));
    base_name = syscall_safe_strrchr(upper_candidate, '/');
    if (base_name == NULL)
        base_name = upper_candidate;
    else
        base_name++;
    dll_ascii_upper_copy((char *)base_name,
                         sizeof(upper_candidate) - (size_t)(base_name - upper_candidate),
                         base_name);
    if (syscall_safe_strcmp(upper_candidate, candidate) != 0 &&
        syscall_safe_path_exists(upper_candidate)) {
        syscall_safe_copy_str(path, upper_candidate, path_size);
        return 1;
    }

    if (dll_name_has_extension(dll_name))
        return 0;

    len = syscall_safe_strlen(candidate);
    if (len + 4 >= sizeof(candidate))
        return 0;
    candidate[len + 0] = '.';
    candidate[len + 1] = 'd';
    candidate[len + 2] = 'l';
    candidate[len + 3] = 'l';
    candidate[len + 4] = '\0';
    if (syscall_safe_path_exists(candidate)) {
        syscall_safe_copy_str(path, candidate, path_size);
        return 1;
    }
    syscall_safe_copy_str(upper_candidate, candidate, sizeof(upper_candidate));
    base_name = syscall_safe_strrchr(upper_candidate, '/');
    if (base_name == NULL)
        base_name = upper_candidate;
    else
        base_name++;
    dll_ascii_upper_copy((char *)base_name,
                         sizeof(upper_candidate) - (size_t)(base_name - upper_candidate),
                         base_name);
    if (syscall_safe_strcmp(upper_candidate, candidate) != 0 &&
        syscall_safe_path_exists(upper_candidate)) {
        syscall_safe_copy_str(path, upper_candidate, path_size);
        return 1;
    }

    candidate[len + 1] = 'D';
    candidate[len + 2] = 'L';
    candidate[len + 3] = 'L';
    if (syscall_safe_path_exists(candidate)) {
        syscall_safe_copy_str(path, candidate, path_size);
        return 1;
    }

    return 0;
}

static int try_dll_candidate_in_dir(const char *dir, const char *dll_name,
                                    char *path, size_t path_size)
{
    char upper_name[128];

    if (syscall_safe_build_path(path, path_size, dir, dll_name) == 0 &&
        syscall_safe_path_exists(path)) {
        return 1;
    }

    dll_ascii_upper_copy(upper_name, sizeof(upper_name), dll_name);
    if (syscall_safe_strcmp(upper_name, dll_name) != 0 &&
        syscall_safe_build_path(path, path_size, dir, upper_name) == 0 &&
        syscall_safe_path_exists(path)) {
        return 1;
    }

    return 0;
}

/**
 * Initialize g_exe_dir with the directory of the main PE file.
 * Called once on first use. Matches Windows behavior where the app directory
 * is searched for DLLs.
 */
static void init_exe_dir(void)
{
    if (g_exe_dir[0] != '\0') return;
    const char *pe_path = loader_get_pe_path();
    if (pe_path != NULL && pe_path[0] != '\0') {
        const char *last_slash = syscall_safe_strrchr(pe_path, '/');
        if (last_slash != NULL && last_slash != pe_path) {
            size_t dir_len = last_slash - pe_path;
            if (dir_len >= sizeof(g_exe_dir)) dir_len = sizeof(g_exe_dir) - 1;
            syscall_safe_memcpy(g_exe_dir, pe_path, dir_len);
            g_exe_dir[dir_len] = '\0';
            return;
        }
    }
    /* Fallback to CWD — use "." since getcwd needs glibc */
    g_exe_dir[0] = '.';
    g_exe_dir[1] = '\0';
}

/**
 * Find the full path to a DLL.
 * Searches: current directory, app directory, WINE_DLL_PATH (semicolon-separated).
 *
 * @param  dll_name   the DLL name to find (e.g. "kernel32.dll")
 * @param  path       buffer to receive the full path
 * @param  path_size  size of the path buffer
 * @return 1 if found, 0 if not found
 */
int find_dll_path(const char *dll_name, char *path, size_t path_size)
{
    char dll_with_ext[128];
    const char *dll_candidates[3];
    int dll_candidate_count = 0;
    int ci;

    syscall_safe_debug_write_str(2, "find_dll_path: name=", dll_name);

    if (dll_name_has_path_separator(dll_name)) {
        if (try_dll_explicit_path(dll_name, path, path_size)) {
            syscall_safe_debug_write_str(2, "find_dll_path: explicit=", "ok");
            return 1;
        }
        syscall_safe_debug_write_str(2, "find_dll_path: explicit=", "not_found");
        return 0;
    }

    dll_candidates[dll_candidate_count++] = dll_name;
    if (!dll_name_has_extension(dll_name)) {
        size_t len = syscall_safe_strlen(dll_name);
        if (len + 4 < sizeof(dll_with_ext)) {
            syscall_safe_copy_str(dll_with_ext, dll_name, sizeof(dll_with_ext));
            dll_with_ext[len + 0] = '.';
            dll_with_ext[len + 1] = 'd';
            dll_with_ext[len + 2] = 'l';
            dll_with_ext[len + 3] = 'l';
            dll_with_ext[len + 4] = '\0';
            dll_candidates[dll_candidate_count++] = dll_with_ext;
        }
    }

    /* --- Try current directory --- */
    for (ci = 0; ci < dll_candidate_count; ci++) {
        if (try_dll_candidate_in_dir(".", dll_candidates[ci], path, path_size)) {
            syscall_safe_debug_write_str(2, "find_dll_path: cwd=", "ok");
            return 1;
        }
    }

    /* --- Try app directory --- */
    init_exe_dir();
    if (g_exe_dir[0] != '.' || g_exe_dir[1] != '\0') {
        for (ci = 0; ci < dll_candidate_count; ci++) {
            if (try_dll_candidate_in_dir(g_exe_dir, dll_candidates[ci], path, path_size)) {
                syscall_safe_debug_write_str(2, "find_dll_path: app=", "ok");
                return 1;
            }
        }
    }

    /* --- Try WINE_DLL_PATH (semicolon-separated) from cached path ---
     * Cached from environ in main() before GS switch — syscall-safe. */
    {
        if (g_wine_dll_path[0] != '\0') {
            #define DLL_PATH_MAX_SEGMENTS 32
            char path_buf[1024];
            const char *segments[DLL_PATH_MAX_SEGMENTS];
            int seg_count = 0;

            syscall_safe_copy_str(path_buf, g_wine_dll_path, sizeof(path_buf));

            char *p = path_buf;
            while (seg_count < DLL_PATH_MAX_SEGMENTS && p != NULL) {
                const char *semi = syscall_safe_strchr(p, ';');
                if (semi != NULL) {
                    *(char *)semi = '\0';
                    segments[seg_count++] = p;
                    p = (char *)semi + 1;
                } else {
                    if (*p != '\0') {
                        segments[seg_count++] = p;
                    }
                    break;
                }
            }

            int i;
            for (i = 0; i < seg_count; i++) {
                for (ci = 0; ci < dll_candidate_count; ci++) {
                    if (try_dll_candidate_in_dir(segments[i], dll_candidates[ci],
                                                 path, path_size)) {
                        syscall_safe_debug_write_str(2, "find_dll_path: wine_path=", "ok");
                        return 1;
                    }
                }
            }
        }
    }

    syscall_safe_debug_write_str(2, "find_dll_path: ret=", "not_found");
    return 0;
}
