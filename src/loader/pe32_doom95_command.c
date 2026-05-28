/*
 * pe32_doom95_command.c -- Doom95 command-array setup helpers.
 */

#include <string.h>
#include <unistd.h>

#include "pe32_doom95_command.h"

static void pe32_build_doom95_basewad_path(const char *path,
                                           char *basewad_path,
                                           size_t basewad_path_size)
{
    const char *slash;
    size_t dir_len;

    slash = strrchr(path, '/');
    if (slash == NULL)
        slash = path - 1;
    dir_len = (size_t)(slash - path + 1);
    if (path[0] != '/') {
        char cwd[512];
        size_t cwd_len;

        if (getcwd(cwd, sizeof(cwd)) != NULL) {
            cwd_len = strlen(cwd);
            if (cwd_len > 0 && cwd[cwd_len - 1] == '/')
                cwd_len--;
            if (cwd_len + 1 + dir_len < basewad_path_size) {
                memcpy(basewad_path, cwd, cwd_len);
                basewad_path[cwd_len] = '/';
                memcpy(basewad_path + cwd_len + 1, path, dir_len);
                basewad_path[cwd_len + 1 + dir_len] = '\0';
            }
        }
    }
    if (basewad_path[0] == '\0') {
        if (dir_len >= basewad_path_size)
            dir_len = basewad_path_size - 1;
        memcpy(basewad_path, path, dir_len);
        basewad_path[dir_len] = '\0';
    }
    strncat(basewad_path, "DOOM1.WAD",
            basewad_path_size - strlen(basewad_path) - 1);
}

void pe32_seed_doom95_command_array(const char *path,
                                    char *basewad_path,
                                    size_t basewad_path_size,
                                    const char *basewad_option,
                                    uint8_t *command_array,
                                    size_t command_array_size)
{
    uint32_t *command_slots;

    memset(command_array, 0, command_array_size);
    pe32_build_doom95_basewad_path(path, basewad_path, basewad_path_size);

    command_slots = (uint32_t *)command_array;
    command_slots[0] = (uint32_t)(uintptr_t)basewad_option;
    command_slots[1] = (uint32_t)(uintptr_t)basewad_path;
    command_slots[2] = 0;
}
