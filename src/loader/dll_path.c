/*
 * dll_path.c — DLL path resolution
 *
 * Searches for DLLs in current directory, app directory, and WINE_DLL_PATH.
 * Extracted from import_resolve.c.
 */

#include "include/common.h"
#include "loader_utils.h"
#include "image_mapper.h"
#include "dll_path.h"
#include "../syscall/syscalls_inline.h"

static char g_exe_dir[512] = {0};

/* ── Debug helpers (syscall-safe, no glibc) ── */
static inline void dbg_fmt_hex(char *dst, uintptr_t val)
{
    for (int i = 7; i >= 0; i--) {
        dst[i] = "0123456789abcdef"[val & 0xf];
        val >>= 4;
    }
}

static inline void dbg_write_ptr(const char *prefix, uintptr_t val)
{
    char buf[64];
    int i = 0;
    const char *p;
    for (p = prefix; *p; ) buf[i++] = *p++;
    buf[i++] = '0'; buf[i++] = 'x';
    dbg_fmt_hex(buf + i, val);
    i += 8;
    buf[i++] = '\n';
    INLINE_SYSCALL_WRITE(2, buf, i);
}

static inline void dbg_write_str(const char *prefix, const char *str)
{
    char buf[256];
    int i = 0;
    const char *p;
    for (p = prefix; *p && i < 240; ) buf[i++] = *p++;
    for (p = str; *p && i < 250; ) buf[i++] = *p++;
    buf[i++] = '\n';
    INLINE_SYSCALL_WRITE(2, buf, i);
}

/**
 * Initialize g_exe_dir with the directory of the main PE file.
 * Called once on first use. Matches Windows behavior where the app directory
 * is searched for DLLs.
 */
static void init_exe_dir(void)
{
    if (g_exe_dir[0] != '\0') return;
    const char *pe_path = get_pe_path();
    if (pe_path != NULL && pe_path[0] != '\0') {
        const char *last_slash = dll_strrchr(pe_path, '/');
        if (last_slash != NULL && last_slash != pe_path) {
            size_t dir_len = last_slash - pe_path;
            if (dir_len >= sizeof(g_exe_dir)) dir_len = sizeof(g_exe_dir) - 1;
            dll_memcpy(g_exe_dir, pe_path, dir_len);
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
    dbg_write_str("find_dll_path: name=", dll_name);

    /* --- Try current directory --- */
    if (dll_build_path(path, path_size, ".", dll_name) == 0) {
        dbg_write_str("find_dll_path: try_cwd=", path);
        if (dll_path_exists(path)) {
            dbg_write_str("find_dll_path: cwd=", "ok");
            return 1;
        }
    }

    /* --- Try app directory --- */
    init_exe_dir();
    if (g_exe_dir[0] != '.' || g_exe_dir[1] != '\0') {
        if (dll_build_path(path, path_size, g_exe_dir, dll_name) == 0) {
            dbg_write_str("find_dll_path: try_app=", path);
            if (dll_path_exists(path)) {
                dbg_write_str("find_dll_path: app=", "ok");
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

            dll_copy_str(path_buf, g_wine_dll_path, sizeof(path_buf));

            char *p = path_buf;
            while (seg_count < DLL_PATH_MAX_SEGMENTS && p != NULL) {
                const char *semi = dll_strchr(p, ';');
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
                if (dll_build_path(path, path_size, segments[i], dll_name) == 0) {
                    dbg_write_str("find_dll_path: try_path=", path);
                    if (dll_path_exists(path)) {
                        dbg_write_str("find_dll_path: wine_path=", "ok");
                        return 1;
                    }
                }
            }
        }
    }

    dbg_write_str("find_dll_path: ret=", "not_found");
    return 0;
}
