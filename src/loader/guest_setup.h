/*
 * guest_setup.h — Guest entry and cleanup
 *
 * Declares the guest entry trampoline, guest setup runner,
 * and guest cleanup functions.
 */

#ifndef MY_WINE_GUEST_SETUP_H
#define MY_WINE_GUEST_SETUP_H

#include <stdint.h>

__attribute__((noreturn)) void run_guest_entry(uint64_t entry_abs, void *image_base, void *stack_top,
                                                void *teb, char **guest_argv,
                                                char **guest_envp);
__attribute__((noreturn)) void setup_guest_and_run(uint64_t entry_abs, void *image_base, void *stack_top,
                                                    void *teb, char **guest_argv,
                                                    char **guest_envp);
void cleanup_guest(void *teb, void *stack_base);

#endif /* MY_WINE_GUEST_SETUP_H */
