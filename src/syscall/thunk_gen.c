#define _GNU_SOURCE
#include <sys/mman.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>

#include "include/syscall/thunk_gen.h"
#include "include/syscall/signal_handler.h"

/*
 * syscall_gen.c — Syscall thunk generator
 *
 * Generates machine code for syscall thunks at runtime. Each thunk encodes:
 *   mov r10, rcx       — Windows x64 calling convention puts 1st arg in RCX,
 *                        Linux syscall uses R10
 *   mov eax, NR + 0xF000 — syscall number with Wine's 0xF000 offset
 *   syscall            — the syscall instruction
 *   ret                — return
 */

#define THUNK_SIZE 11
#define THUNK_PAGE 4096

#define NUM_NT_SYSCALLS 16

/*
 * Function pointer type for a thunk.
 * Thunks are called via the Linux syscall calling convention (RDI, RSI, RDX, R10/RAX).
 * The return value is stored in RAX (long on x86_64).
 */
typedef long (*thunk_fn)(void);

/* Global array indexed by Windows syscall number. */
static thunk_fn thunk_array[0x50] = { 0 };

/* List of NT syscall numbers we support. */
static const uint16_t nt_syscall_list[] = {
    0x05, 0x07, 0x0F, 0x18, 0x19, 0x24, 0x26, 0x28,
    0x29, 0x2A, 0x3C, 0x3D, 0x48, 0x4A, 0x4E, 0x4F
};

/* signal_handler.h provides the thunk registration function */

/*
 * generate_thunk — allocate and write a syscall thunk
 *
 * @syscall_number: Windows NT syscall number (e.g. 0x3D for NtWriteFile).
 *                  The actual syscall number emitted is syscall_number + 0xF000.
 *
 * Returns a void* to the executable memory, or NULL on failure.
 */
void *generate_thunk(uint16_t syscall_number)
{
    uint32_t actual_nr = (uint32_t)(syscall_number + 0xF000);
    uint8_t code[THUNK_SIZE] = {
        0x41, 0x89, 0xCF,                       /* mov r10, rcx */
        0xB8,
        (uint8_t)(actual_nr & 0xFF),            /* mov eax, actual_nr */
        (uint8_t)((actual_nr >> 8) & 0xFF),
        (uint8_t)((actual_nr >> 16) & 0xFF),
        (uint8_t)((actual_nr >> 24) & 0xFF),
        0x0F, 0x05,                              /* syscall */
        0xC3                                     /* ret */
    };

    void *mem = mmap(NULL, THUNK_PAGE,
                     PROT_READ | PROT_WRITE | PROT_EXEC,
                     MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (mem == MAP_FAILED) {
        perror("mmap");
        return NULL;
    }

    memcpy(mem, code, THUNK_SIZE);

    register_thunk_addr(mem);

    return mem;
}

/*
 * generate_all_thunks — generates thunks for all 16 NT syscalls.
 *
 * Populates thunk_array indexed by syscall number.
 * Returns the thunk_array pointer (or NULL on failure).
 */
void **generate_all_thunks(void)
{
    for (int i = 0; i < NUM_NT_SYSCALLS; i++) {
        uint16_t nr = nt_syscall_list[i];
        void *thunk = generate_thunk(nr);
        if (thunk == NULL) {
            fprintf(stderr, "generate_all_thunks: failed for syscall 0x%02X\n", nr);
            return NULL;
        }
        thunk_array[nr] = (thunk_fn)thunk;
    }
    return (void **)thunk_array;
}

/*
 * lookup_thunk — returns the thunk for a given syscall number.
 * Generates on first use if not yet cached.
 *
 * Returns the thunk address, or NULL if syscall is not supported or generation failed.
 */
void *lookup_thunk(uint16_t syscall_number)
{
    if (syscall_number >= sizeof(thunk_array) / sizeof(thunk_array[0]))
        return NULL;

    /* Return cached thunk if already generated. */
    if (thunk_array[syscall_number] != NULL)
        return (void *)thunk_array[syscall_number];

    /* Generate on first use. */
    void *thunk = generate_thunk(syscall_number);
    if (thunk == NULL)
        return NULL;

    thunk_array[syscall_number] = (thunk_fn)thunk;
    return thunk;
}
