/*
 * thunk_gen.h — Syscall thunk generator
 *
 * Generates machine code for syscall thunks at runtime and
 * provides a lookup table indexed by NT syscall number.
 */

#ifndef SYSCALL_THUNK_GEN_H
#define SYSCALL_THUNK_GEN_H

#include <stdint.h>

/**
 * Generate thunks for all supported NT syscalls.
 * Returns a pointer to the thunk array indexed by syscall number,
 * or NULL on failure.
 */
void **generate_all_thunks(void);

/**
 * Look up (or lazily generate) the thunk for a given syscall number.
 * Returns the thunk address, or NULL if unsupported or generation failed.
 */
void *lookup_thunk(uint16_t syscall_number);

#endif /* SYSCALL_THUNK_GEN_H */
