/*
 * pe32_doom95_compat.c -- Doom95-specific PE32 runtime shaping.
 *
 * This code is intentionally sample-specific. Keep generic PE32 launch policy
 * out of this file.
 */

#include <string.h>
#include <unistd.h>

#include "include/common.h"
#include "include/handle_manager.h"
#include "include/nt_constants.h"
#include "include/pe_parser.h"
#include "src/pe_priv.h"
#include "../syscall/syscalls_inline.h"
#include "pe32_doom95_compat.h"

enum {
    DOOM95_STD_HANDLE_COUNT_RVA = 0x218358,
    DOOM95_STD_HANDLE_TABLE_RVA = 0x21835c,
    DOOM95_TRAP_FLAG_RVA        = 0x077d84,
    DOOM95_COMMAND_ARRAY_RVA    = 0x0805d0,
    DOOM95_COMMAND_ARRAY_BYTES  = 0x100,
};

bool pe32_is_doom95_path(const char *path)
{
    const char *name;

    if (path == NULL)
        return false;

    name = strrchr(path, '/');
    name = name ? name + 1 : path;
    return strcmp(name, "DOOM95.EXE") == 0;
}

void pe32_apply_doom95_runtime_compat(const char *path,
                                      void *image_base,
                                      IMAGE_NT_HEADERS *nt,
                                      char *basewad_path,
                                      size_t basewad_path_size,
                                      const char *basewad_option)
{
    uint32_t *std_handle_count;
    uint32_t *std_handle_table_slot;
    uint32_t *command_slots;
    uint8_t *trap_flag;
    uint8_t *command_array;
    uint32_t *guest_table;
    void *table_page;
    const char *slash;
    size_t dir_len;

    if (!pe32_is_doom95_path(path))
        return;

    trap_flag = pe_rva_to_ptr(image_base, nt, DOOM95_TRAP_FLAG_RVA, sizeof(uint8_t));
    std_handle_count = pe_rva_to_ptr(image_base, nt, DOOM95_STD_HANDLE_COUNT_RVA,
                                     sizeof(uint32_t));
    std_handle_table_slot = pe_rva_to_ptr(image_base, nt, DOOM95_STD_HANDLE_TABLE_RVA,
                                          sizeof(uint32_t));
    command_array = pe_rva_to_ptr(image_base, nt, DOOM95_COMMAND_ARRAY_RVA,
                                  DOOM95_COMMAND_ARRAY_BYTES);
    if (trap_flag == NULL || std_handle_count == NULL ||
        std_handle_table_slot == NULL || command_array == NULL) {
        return;
    }

    table_page = INLINE_SYSCALL_MMAP(NULL, PAGE_SIZE, PROT_READ | PROT_WRITE,
                                     MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (table_page == MAP_FAILED)
        return;

    guest_table = (uint32_t *)table_page;
    guest_table[0] = (uint32_t)STDIN_HANDLE;
    guest_table[1] = (uint32_t)STDOUT_HANDLE;
    guest_table[2] = (uint32_t)STDERR_HANDLE;

    *trap_flag = 0;
    *std_handle_count = 3;
    *std_handle_table_slot = (uint32_t)(uintptr_t)guest_table;
    memset(command_array, 0, DOOM95_COMMAND_ARRAY_BYTES);

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

    command_slots = (uint32_t *)command_array;
    command_slots[0] = (uint32_t)(uintptr_t)basewad_option;
    command_slots[1] = (uint32_t)(uintptr_t)basewad_path;
    command_slots[2] = 0;
}
