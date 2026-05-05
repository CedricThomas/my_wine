#define _GNU_SOURCE
#include <sys/mman.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <inttypes.h>
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
 * Each thunk encodes (23 bytes):
 *   push rdi                — save original RDI (2 bytes)
 *   mov rdi, imm32(NT_NR)   — put the NT syscall number in RDI (7 bytes)
 *   mov rax, imm64(dispatcher) — load dispatcher address (10 bytes)
 *   call rax                — jump to dispatcher (2 bytes)
 *   pop rdi                 — restore original RDI (1 byte)
 *   ret                     — return to guest caller (1 byte)
 *
 * Uses absolute indirect call (mov rax; call rax) instead of relative call
 * (E8 displacement) to handle ASLR: the thunk blob and dispatcher can be
 * more than 2 GB apart when both are independently randomized.
 *
 * RDI is saved/restored here (not in the dispatcher) so the dispatcher's
 * return goes to `pop rdi` instead of having to skip past the thunk.
 *
 * All thunks live in a single mmap'd executable blob.
 */

#define THUNK_SIZE 23        /* 2+7+10+2+1+1 bytes: push+mov+rax+call+pop+ret */
#define NUM_NT_SYSCALLS 25

static void *thunk_blob = NULL;    /* single mmap'd executable region */
static size_t thunk_blob_size = 0;

typedef void (*thunk_fn)(void);
static thunk_fn thunk_array[0x60] = { 0 };

static const uint16_t nt_syscall_list[] = {
    NT_SYSCALL_CALLBACK_RETURN, NT_SYSCALL_QUERY_INFO_PROCESS,
    NT_SYSCALL_CLOSE, NT_SYSCALL_ALLOC_VM, NT_SYSCALL_FREE_VM,
    NT_SYSCALL_GET_CTX_THREAD, NT_SYSCALL_SET_CTX_THREAD,
    NT_SYSCALL_MAP_VIEW, NT_SYSCALL_UNMAP_VIEW,
    NT_SYSCALL_TERMINATE_PROCESS,
    NT_SYSCALL_READ_FILE, NT_SYSCALL_WRITE_FILE,
    NT_SYSCALL_CREATE_EVENT, NT_SYSCALL_CREATE_SECTION,
    NT_SYSCALL_CREATE_THREAD_EX, NT_SYSCALL_OPEN_FILE,
    NT_SYSCALL_QUERY_SYSTEM_TIME, NT_SYSCALL_DELAY_EXECUTION,
    NT_SYSCALL_QUERY_PERFORMANCE_COUNTER, NT_SYSCALL_QUERY_PERFORMANCE_FREQUENCY,
    NT_SYSCALL_WAIT_FOR_SINGLE_OBJECT, NT_SYSCALL_RELEASE_MUTEX,
    NT_SYSCALL_CREATE_MUTEX, NT_SYSCALL_SET_EVENT, NT_SYSCALL_RESET_EVENT
};

/*
 * write_thunk_at — encode a 23-byte thunk at the given location.
 * Layout:
 *   Offset 0-1:   push rdi          (save original RDI)
 *   Offset 2-8:   mov rdi, imm32    (set syscall number)
 *   Offset 9-18:  mov rax, imm64    (load dispatcher address, absolute)
 *   Offset 19-20: call rax          (enter dispatcher via indirect call)
 *   Offset 21:    pop rdi           (restore original RDI, after dispatcher returns)
 *   Offset 22:    ret               (return to guest caller)
 */
static void write_thunk_at(uint8_t *loc, uint16_t syscall_number, void *dispatcher_addr)
{
    if (dispatcher_addr == NULL) {
        fprintf(stderr, "wine: fatal: dispatcher address is NULL, cannot generate thunks\n");
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

    /* mov rax, imm64(dispatcher) — 10 bytes: 48 B8 XX XX XX XX XX XX XX XX
     * Absolute indirect call — no displacement overflow risk regardless of ASLR */
    loc[9] = 0x48;        /* REX.W */
    loc[10] = 0xB8;       /* mov rax, imm64 */
    uint64_t addr = (uint64_t)(uintptr_t)dispatcher_addr;
    loc[11] = (uint8_t)(addr & 0xFF);
    loc[12] = (uint8_t)((addr >> 8) & 0xFF);
    loc[13] = (uint8_t)((addr >> 16) & 0xFF);
    loc[14] = (uint8_t)((addr >> 24) & 0xFF);
    loc[15] = (uint8_t)((addr >> 32) & 0xFF);
    loc[16] = (uint8_t)((addr >> 40) & 0xFF);
    loc[17] = (uint8_t)((addr >> 48) & 0xFF);
    loc[18] = (uint8_t)((addr >> 56) & 0xFF);

    DEBUG("wine: thunk[%d] dispatcher=0x%lx", syscall_number, (unsigned long)dispatcher_addr);

    /* call rax — 2 bytes: FF D0 (indirect call through RAX) */
    loc[19] = 0xFF;
    loc[20] = 0xD0;

    /* pop rdi — 1 byte: 5F (restore original RDI after dispatcher returns) */
    loc[21] = 0x5F;

    /* ret — 1 byte: C3 (return to guest caller) */
    loc[22] = 0xC3;
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
        fprintf(stderr, "wine: fatal: __wine_dispatcher symbol not found\n");
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
