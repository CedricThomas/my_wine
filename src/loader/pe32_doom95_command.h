/*
 * pe32_doom95_command.h -- Doom95 command-array setup helpers.
 *
 * This keeps the guest-visible "-basewad" argument construction separate from
 * the rest of the Doom95 runtime shaping.
 */

#ifndef MY_WINE_PE32_DOOM95_COMMAND_H
#define MY_WINE_PE32_DOOM95_COMMAND_H

#include <stddef.h>
#include <stdint.h>

void pe32_seed_doom95_command_array(const char *path,
                                    char *basewad_path,
                                    size_t basewad_path_size,
                                    const char *basewad_option,
                                    uint8_t *command_array,
                                    size_t command_array_size);

#endif /* MY_WINE_PE32_DOOM95_COMMAND_H */
