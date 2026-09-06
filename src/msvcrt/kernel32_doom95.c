/*
 * kernel32_doom95.c
 *
 * Doom95-only kernel32/runtime compatibility helpers. Keep guest-layout
 * knowledge and launcher-facing runtime shaping here rather than in generic
 * user32 or path helpers.
 */

#include <stdint.h>
#include <string.h>

#include "kernel32_doom95.h"
#include "kernel32_priv.h"

#define DOOM95_BASEWAD_SLOT_OFFSET 0x131e0u
#define DOOM95_BASEWAD_STATE_OFFSET 0x23u

size_t wine_build_doom95_basewad_path(char *dst, size_t dst_size,
                                      const char *directory)
{
    static const char doom95_basewad_name[] = "DOOM1.WAD";
    size_t basewad_len = sizeof(doom95_basewad_name) - 1;
    size_t dir_len = 0;
    int needs_slash = 0;

    if (!dst || dst_size == 0)
        return 0;

    if (directory && directory[0] != '\0') {
        dir_len = strlen(directory);
        needs_slash = directory[dir_len - 1] != '/';
        if (dir_len + (size_t)needs_slash + basewad_len < dst_size) {
            memcpy(dst, directory, dir_len);
            if (needs_slash)
                dst[dir_len++] = '/';
            memcpy(dst + dir_len, doom95_basewad_name, basewad_len + 1);
            return dir_len + basewad_len;
        }
    }

    if (basewad_len >= dst_size)
        basewad_len = dst_size - 1;
    memcpy(dst, doom95_basewad_name, basewad_len);
    dst[basewad_len] = '\0';
    return basewad_len;
}

void wine_doom95_seed_basewad_state(uintptr_t module_handle)
{
    uintptr_t *state_slot;
    uintptr_t state;
    char *basewad;

    if (module_handle == 0)
        return;

    state_slot = (uintptr_t *)(module_handle + DOOM95_BASEWAD_SLOT_OFFSET);
    state = *state_slot;
    if (state == 0)
        return;

    basewad = (char *)(uintptr_t)(state + DOOM95_BASEWAD_STATE_OFFSET);
    wine_build_doom95_basewad_path(basewad, 0x100u,
                                   wine_get_current_directory());
}
