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
 * Each thunk encodes (16 bytes):
 *   push rdi               — save original RDI (2 bytes)
 *   mov rdi, imm32(NT_NR)  — put the NT syscall number in RDI (7 bytes)
 *   call __wine_dispatcher — jump to dispatcher (5 bytes)
 *   pop rdi                — restore original RDI (1 byte)
 *   ret                    — return to guest caller (1 byte)
 *
 * RDI is saved/restored here (not in the dispatcher) so the dispatcher's
 * return goes to `pop rdi` instead of having to skip past the thunk.
 *
 * All thunks live in a single mmap'd executable blob.
 */

#define THUNK_SIZE 16        /* 2+7+5+1+1 bytes: push+mov+call+pop+ret */
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
 * write_thunk_at — encode a 16-byte thunk at the given location.
 * Layout:
 *   Offset 0-1:   push rdi          (save original RDI)
 *   Offset 2-8:   mov rdi, imm32    (set syscall number)
 *   Offset 9-13:  call dispatcher   (enter dispatcher; RIP after call = loc+14)
 *   Offset 14:    pop rdi           (restore original RDI, after dispatcher returns)
 *   Offset 15:    ret               (return to guest caller)
 */
static void write_thunk_at(uint8_t *loc, uint16_t syscall_number, void *dispatcher_addr)
{
    if (dispatcher_addr == NULL) {
        fprintf(stderr, "wine: fatal: dispatcher address is NULL\n");
        abort();
    }

    /* push rdi — 2 bytes: 41 57 (REX.B + push rdi) */
    loc[0] = 0x41;
    loc[1] = 0x57;

    /* mov rdi, imm32(NT_NR) — 7 bytes: 48 C7 C7 XX XX XX XX */
    loc[2] = 0x48;        /* REX.W */
    loc[3] = 0xC7;        /* mov rdi, imm32 */
    loc[4] = 0xC7;
    loc[5] = (uint8_t)(syscall_number & 0xFF);
    loc[6] = (uint8_t)((syscall_number >> 8) & 0xFF);
    loc[7] = (uint8_t)((syscall_number >> 16) & 0xFF);
    loc[8] = (uint8_t)((syscall_number >> 24) & 0xFF);

    /* call __wine_dispatcher — 5 bytes: E8 XX XX XX XX
     * RIP after the 5-byte call = loc + 14, so displacement is relative to that */
    loc[9] = 0xE8;
    int64_t raw_disp = (int64_t)(uintptr_t)dispatcher_addr - (int64_t)(uintptr_t)(loc + 14);
    if (raw_disp > INT32_MAX || raw_disp < INT32_MIN) {
        fprintf(stderr, "wine: fatal: dispatcher displacement %lld out of range for near call (must be within ±2GB)\n", (long long)raw_disp);
        abort();
    }
    int32_t disp = (int32_t)raw_disp;
    loc[10] = (uint8_t)(disp & 0xFF);
    loc[11] = (uint8_t)((disp >> 8) & 0xFF);
    loc[12] = (uint8_t)((disp >> 16) & 0xFF);
    loc[13] = (uint8_t)((disp >> 24) & 0xFF);

    /* pop rdi — 1 byte: 5F (restore original RDI after dispatcher returns) */
    loc[14] = 0x5F;

    /* ret — 1 byte: C3 (return to guest caller) */
    loc[15] = 0xC3;
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
    printf("wine: dispatcher at %p\n", dispatcher);
    if (dispatcher == NULL) {
        fprintf(stderr, "wine: fatal: cannot resolve __wine_dispatcher\n");
        return NULL;
    }

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
