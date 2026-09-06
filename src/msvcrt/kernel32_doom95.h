#ifndef MY_WINE_KERNEL32_DOOM95_H
#define MY_WINE_KERNEL32_DOOM95_H

#include <stddef.h>
#include <stdint.h>

size_t wine_build_doom95_basewad_path(char *dst, size_t dst_size,
                                      const char *directory);
void wine_doom95_seed_basewad_state(uintptr_t module_handle);

#endif /* MY_WINE_KERNEL32_DOOM95_H */
