#define _GNU_SOURCE
#include <sys/mman.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include "include/syscall/thunk_gen.h"
#include "include/syscall/dispatcher_entry.h"
#include "include/nt_constants.h"
#include "include/common.h"
#include "../syscalls_inline.h"

/*
 * thunk_gen.c — Direct-call thunk generator
 *
 * Generates machine code for thunks that call __wine_dispatcher directly.
 * Each thunk encodes:
 *   mov rdi, imm32(NT_NR)  — put the NT syscall number in RDI (7 bytes)
 *   call __wine_dispatcher — jump to dispatcher (5 bytes)
 *
 * All thunks live in a single mmap'd executable blob.
 */

#define THUNK_SIZE 12        /* 7 bytes mov rdi,imm32 + 5 bytes call */
#define NUM_NT_SYSCALLS 16

static void *thunk_blob = NULL;    /* single mmap'd executable region */
static size_t thunk_blob_size = 0;

typedef void (*thunk_fn)(void);
static thunk_fn thunk_array[0x50] = { 0 };

static const uint16_t nt_syscall_list[] = {
    NT_SYSCALL_CALLBACK_RETURN, NT_SYSCALL_QUERY_INFO_PROCESS,
    NT_SYSCALL_CLOSE, NT_SYSCALL_ALLOC_VM, NT_SYSCALL_FREE_VM,
    NT_SYSCALL_GET_CTX_THREAD, NT_SYSCALL_SET_CTX_THREAD,
    NT_SYSCALL_MAP_VIEW, NT_SYSCALL_UNMAP_VIEW,
    NT_SYSCALL_TERMINATE_PROCESS,
    NT_SYSCALL_READ_FILE, NT_SYSCALL_WRITE_FILE,
    NT_SYSCALL_CREATE_EVENT, NT_SYSCALL_CREATE_SECTION,
    NT_SYSCALL_CREATE_THREAD_EX, NT_SYSCALL_OPEN_FILE
};

/*
 * write_thunk_at — encode a 12-byte thunk at the given location.
 */
static void write_thunk_at(uint8_t *loc, uint16_t syscall_number, void *dispatcher_addr)
{
    /* mov rdi, imm32(NT_NR) — 7 bytes: 48 C7 C7 XX XX XX XX */
    loc[0] = 0x48;        /* REX.W */
    loc[1] = 0xC7;        /* mov rdi, imm32 */
    loc[2] = 0xC7;
    loc[3] = (uint8_t)(syscall_number & 0xFF);
    loc[4] = (uint8_t)((syscall_number >> 8) & 0xFF);
    loc[5] = (uint8_t)((syscall_number >> 16) & 0xFF);
    loc[6] = (uint8_t)((syscall_number >> 24) & 0xFF);

    /* call __wine_dispatcher — 5 bytes: E8 XX XX XX XX */
    loc[7] = 0xE8;
    int32_t disp = (uint8_t *)dispatcher_addr - (loc + 12);
    loc[8] = (uint8_t)(disp & 0xFF);
    loc[9] = (uint8_t)((disp >> 8) & 0xFF);
    loc[10] = (uint8_t)((disp >> 16) & 0xFF);
    loc[11] = (uint8_t)((disp >> 24) & 0xFF);
}

/*
 * generate_all_thunks — allocate one executable blob, write all thunks, return array.
 */
void **generate_all_thunks(void)
{
    size_t needed = (size_t)NUM_NT_SYSCALLS * THUNK_SIZE;
    size_t alloc = (needed + PAGE_SIZE - 1) & ~(size_t)(PAGE_SIZE - 1);
    if (alloc < PAGE_SIZE) alloc = PAGE_SIZE;

    thunk_blob = mmap(NULL, alloc, PROT_READ | PROT_WRITE | PROT_EXEC,
                      MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (thunk_blob == MAP_FAILED) {
        perror("mmap thunk_blob");
        return NULL;
    }
    thunk_blob_size = alloc;

    void *dispatcher = wine_dispatcher_addr();

    for (int i = 0; i < NUM_NT_SYSCALLS; i++) {
        uint16_t nr = nt_syscall_list[i];
        uint8_t *loc = (uint8_t *)thunk_blob + ((size_t)i * THUNK_SIZE);
        write_thunk_at(loc, nr, dispatcher);
        thunk_array[nr] = (thunk_fn)loc;
    }

    return (void **)thunk_array;
}

/*
 * lookup_thunk — return the thunk for a given syscall number, or NULL.
 */
void *lookup_thunk(uint16_t syscall_number)
{
    if (syscall_number >= sizeof(thunk_array) / sizeof(thunk_array[0]))
        return NULL;
    return (void *)thunk_array[syscall_number];
}

/*
 * cleanup_thunk_pages — unmap the single thunk blob.
 */
void cleanup_thunk_pages(void)
{
    if (thunk_blob != NULL) {
        INLINE_SYSCALL_MUNMAP(thunk_blob, thunk_blob_size);
        thunk_blob = NULL;
        thunk_blob_size = 0;
    }
}
