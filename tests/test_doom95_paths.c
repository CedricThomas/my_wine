#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include <unistd.h>
#include <sys/stat.h>

#include "src/loader/loader_state.h"
#include "src/msvcrt/kernel32_doom95.h"
#include "src/msvcrt/kernel32_priv.h"

static int g_failures = 0;

#define T(cond, msg)                                                           \
    do {                                                                       \
        if (!(cond)) {                                                         \
            fprintf(stderr, "FAIL: %s\n", msg);                                \
            g_failures++;                                                      \
        }                                                                      \
    } while (0)

static void expect_str_eq(const char *label, const char *actual, const char *expected)
{
    if (actual == NULL || expected == NULL || strcmp(actual, expected) != 0) {
        fprintf(stderr, "FAIL: %s\n", label);
        fprintf(stderr, "  actual:   %s\n", actual ? actual : "(null)");
        fprintf(stderr, "  expected: %s\n", expected ? expected : "(null)");
        g_failures++;
    }
}

static int path_join(char *dst, size_t dst_size, const char *lhs, const char *rhs)
{
    size_t lhs_len;
    size_t rhs_len;

    if (dst == NULL || lhs == NULL || rhs == NULL || dst_size == 0)
        return 0;

    lhs_len = strlen(lhs);
    rhs_len = strlen(rhs);
    if (lhs_len + rhs_len + 1 >= dst_size)
        return 0;

    memcpy(dst, lhs, lhs_len);
    dst[lhs_len] = '/';
    memcpy(dst + lhs_len + 1, rhs, rhs_len + 1);
    return 1;
}

static void test_current_directory_resolution(void)
{
    char original_cwd[PATH_MAX];
    char temp_template[] = "/tmp/my_wine_doom95_pathsXXXXXX";
    char *temp_dir;
    char expected[PATH_MAX];
    char resolved[PATH_MAX];
    char nested_dir[PATH_MAX];

    T(getcwd(original_cwd, sizeof(original_cwd)) != NULL, "getcwd for original cwd failed");

    temp_dir = mkdtemp(temp_template);
    T(temp_dir != NULL, "mkdtemp failed");
    if (!temp_dir)
        return;

    T(path_join(expected, sizeof(expected), temp_dir, "game") == 1,
      "join temp_dir/game failed");
    T(mkdir(expected, 0755) == 0, "mkdir temp_dir/game failed");

    T(path_join(nested_dir, sizeof(nested_dir), expected, "config") == 1,
      "join temp_dir/game/config failed");
    T(mkdir(nested_dir, 0755) == 0, "mkdir temp_dir/game/config failed");

    T(chdir(temp_dir) == 0, "chdir to temp_dir failed");

    loader_set_pe_path(NULL);
    wine_reset_current_directory_cache();
    expect_str_eq("empty PE path falls back to '.'",
                  wine_get_current_directory(), ".");

    loader_set_pe_path("/opt/doom/DOOM95.EXE");
    wine_reset_current_directory_cache();
    expect_str_eq("absolute PE path yields parent directory",
                  wine_get_current_directory(), "/opt/doom");

    loader_set_pe_path("/DOOM95.EXE");
    wine_reset_current_directory_cache();
    expect_str_eq("root PE path yields '/'",
                  wine_get_current_directory(), "/");

    loader_set_pe_path("game/DOOM95.EXE");
    wine_reset_current_directory_cache();
    expect_str_eq("relative PE path resolves against cwd",
                  wine_get_current_directory(), expected);

    T(wine_resolve_path("DOOM1.WAD", resolved, sizeof(resolved)) == 1,
      "wine_resolve_path relative file failed");
    T(path_join(expected, sizeof(expected), temp_dir, "game/DOOM1.WAD") == 1,
      "join temp_dir/game/DOOM1.WAD failed");
    expect_str_eq("relative file resolves under process directory", resolved, expected);

    T(wine_resolve_path("C:\\Games\\DOOM\\DOOM1.WAD", resolved, sizeof(resolved)) == 1,
      "wine_resolve_path drive path failed");
    expect_str_eq("drive path normalizes to unix absolute path",
                  resolved, "/Games/DOOM/DOOM1.WAD");

    T(wine_resolve_path("\\DATA\\DEFAULT.CFG", resolved, sizeof(resolved)) == 1,
      "wine_resolve_path rooted DOS path failed");
    expect_str_eq("rooted DOS path preserves absolute semantics",
                  resolved, "/DATA/DEFAULT.CFG");

    T(wine_set_current_directory(nested_dir) == 1,
      "wine_set_current_directory absolute nested dir failed");
    expect_str_eq("set_current_directory updates cached directory",
                  wine_get_current_directory(), nested_dir);

    T(wine_resolve_path("SETUP.INI", resolved, sizeof(resolved)) == 1,
      "wine_resolve_path after set_current_directory failed");
    T(path_join(expected, sizeof(expected), nested_dir, "SETUP.INI") == 1,
      "join nested_dir/SETUP.INI failed");
    expect_str_eq("relative path uses updated current directory", resolved, expected);

    T(chdir(original_cwd) == 0, "restore original cwd failed");
}

static void test_doom95_basewad_helper(void)
{
    char basewad[32];
    char long_dir[64];
    size_t i;

    expect_str_eq("null directory falls back to bare WAD",
                  ({ wine_build_doom95_basewad_path(basewad, sizeof(basewad), NULL); basewad; }),
                  "DOOM1.WAD");

    expect_str_eq("directory appends base WAD name",
                  ({ wine_build_doom95_basewad_path(basewad, sizeof(basewad), "/opt/doom"); basewad; }),
                  "/opt/doom/DOOM1.WAD");

    expect_str_eq("trailing slash is preserved once",
                  ({ wine_build_doom95_basewad_path(basewad, sizeof(basewad), "/opt/doom/"); basewad; }),
                  "/opt/doom/DOOM1.WAD");

    for (i = 0; i + 1 < sizeof(long_dir); i++)
        long_dir[i] = 'a';
    long_dir[sizeof(long_dir) - 1] = '\0';
    expect_str_eq("oversized directory falls back to bare WAD",
                  ({ wine_build_doom95_basewad_path(basewad, sizeof(basewad), long_dir); basewad; }),
                  "DOOM1.WAD");
}

int main(void)
{
    test_current_directory_resolution();
    test_doom95_basewad_helper();

    if (g_failures == 0)
        printf("PASS: doom95 path helpers\n");
    else
        printf("FAIL: %d test(s) failed\n", g_failures);

    return g_failures;
}
