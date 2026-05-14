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
#include "syscalls_inline.h"

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
 *
 * ARCHITECTURE NOTE: This generated code is x86_64-only.
 *   - All opcodes (push rdi, mov rdi imm32, mov rax imm64, call rax,
 *     pop rdi, ret) are x86_64 machine code instructions.
 *   - The 23-byte thunk layout is specific to x86_64 instruction encoding.
 *   - REX prefixes (0x48, 0x41) used are x86_64-specific.
 *   - This generated code will only execute on x86_64 CPUs.
 */

#if defined(__i386__)
#define THUNK_SIZE  THUNK_SIZE_32   /* 15 bytes for 32-bit thunks */
#else
#define THUNK_SIZE  23              /* 2+7+10+2+1+1 bytes: push+mov+rax+call+pop+ret */
#endif
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

static void validate_dispatcher_addr(void *addr)
{
    if (addr == NULL) {
#ifdef MY_WINE32
        INLINE_SYSCALL_WRITE_ERR("wine: fatal: dispatcher address is NULL, cannot generate thunks\n", sizeof("wine: fatal: dispatcher address is NULL, cannot generate thunks\n") - 1);
        INLINE_SYSCALL_EXIT(1);
#else
        fprintf(stderr, "wine: fatal: dispatcher address is NULL, cannot generate thunks\n");
        abort();
#endif
    }
}

#if defined(__i386__)
/*
 * write_thunk_at_32 — encode a 15-byte 32-bit thunk at the given location.
 * Layout:
 *   Offset 0:     push ebp          (save guest EBP — callee-saved, preserved by dispatcher)
 *   Offset 1-5:   mov eax, imm32    (load dispatcher address, clobbers EAX)
 *   Offset 6-10:  mov edx, imm32    (load syscall number)
 *   Offset 11-12: call eax          (indirect call to dispatcher)
 *   Offset 13:    pop ebp           (restore guest EBP)
 *   Offset 14:    ret               (return to guest caller)
 * Total: 15 bytes: 55 B8 XX XX XX XX BA XX XX XX XX FF D0 5D C3
 *
 * In 32-bit mode all addresses fit in 32 bits, so imm32 encoding is sufficient.
 * EAX carries dispatcher address, EDX carries syscall number to the dispatcher.
 * push ebp/pop ebp wraps the call (like push rdi/pop rdi in 64-bit mode).
 * EBP is callee-saved (already preserved by dispatcher), so EAX is free to
 * carry the syscall return value back to the guest without being clobbered.
 */
#define THUNK_SIZE_32 15

static void write_thunk_at_32(uint8_t *loc, uint16_t syscall_number, void *dispatcher_addr)
{
    validate_dispatcher_addr(dispatcher_addr);

    uint32_t disp_addr = (uint32_t)(uintptr_t)dispatcher_addr;

    /* push ebp — 1 byte: 55 */
    loc[0] = 0x55;

    /* mov eax, imm32(dispatcher) — 5 bytes: B8 XX XX XX XX */
    loc[1] = 0xB8;
    loc[2] = (uint8_t)(disp_addr & 0xFF);
    loc[3] = (uint8_t)((disp_addr >> 8) & 0xFF);
    loc[4] = (uint8_t)((disp_addr >> 16) & 0xFF);
    loc[5] = (uint8_t)((disp_addr >> 24) & 0xFF);

    /* mov edx, imm32(syscall_number) — 5 bytes: BA XX XX XX XX */
    loc[6] = 0xBA;
    uint32_t nr = (uint32_t)syscall_number;
    loc[7] = (uint8_t)(nr & 0xFF);
    loc[8] = (uint8_t)((nr >> 8) & 0xFF);
    loc[9] = (uint8_t)((nr >> 16) & 0xFF);
    loc[10] = (uint8_t)((nr >> 24) & 0xFF);

    DEBUG("wine: thunk[%d] dispatcher=0x%x", syscall_number, (unsigned)disp_addr);

    /* call eax — 2 bytes: FF D0 (indirect call through EAX) */
    loc[11] = 0xFF;
    loc[12] = 0xD0;

    /* pop ebp — 1 byte: 5D (restore guest EBP) */
    loc[13] = 0x5D;

    /* ret — 1 byte: C3 (return to guest caller) */
    loc[14] = 0xC3;
}

#else /* !__i386__ — x86_64 */

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
    validate_dispatcher_addr(dispatcher_addr);

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

#endif /* __i386__ */

/*
 * generate_all_thunks — allocate one executable blob, write all thunks, return array.
 */
void **generate_all_thunks(void)
{
#if defined(__i386__)
    size_t needed = (size_t)NUM_NT_SYSCALLS * THUNK_SIZE_32;
#else
    size_t needed = (size_t)NUM_NT_SYSCALLS * THUNK_SIZE;
#endif
    size_t alloc = (needed + PAGE_SIZE - 1) & ~(size_t)(PAGE_SIZE - 1);
    if (alloc < PAGE_SIZE) alloc = PAGE_SIZE;

#ifdef MY_WINE32
    thunk_blob = INLINE_SYSCALL_MMAP(NULL, alloc, PROT_READ | PROT_WRITE | PROT_EXEC,
                      MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (thunk_blob == MAP_FAILED) {
        INLINE_SYSCALL_WRITE_ERR("mmap thunk_blob\n", sizeof("mmap thunk_blob\n") - 1);
        return NULL;
    }
#else
    thunk_blob = mmap(NULL, alloc, PROT_READ | PROT_WRITE | PROT_EXEC,
                      MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (thunk_blob == MAP_FAILED) {
        perror("mmap thunk_blob");
        return NULL;
    }
#endif
    thunk_blob_size = alloc;

    void *dispatcher = wine_dispatcher_addr();

#ifdef MY_WINE32
    {
        char msg[64];
        int off = 0;
        const char prefix[] = "wine: dispatcher at 0x";
        int i;
        for (i = 0; prefix[i]; i++) msg[off++] = prefix[i];
        uint64_t val = (uint64_t)(uintptr_t)dispatcher;
        format_hex(msg + off, sizeof(msg) - off, val);
        off += 16;
        msg[off++] = '\n';
        INLINE_SYSCALL_WRITE_ERR(msg, (size_t)off);
    }
    if (dispatcher == NULL) {
        INLINE_SYSCALL_WRITE_ERR("wine: fatal: __wine_dispatcher symbol not found\n", sizeof("wine: fatal: __wine_dispatcher symbol not found\n") - 1);
        return NULL;
    }
#else
    fprintf(stderr, "wine: dispatcher at %p\n", dispatcher);
    if (dispatcher == NULL) {
        fprintf(stderr, "wine: fatal: __wine_dispatcher symbol not found\n");
        return NULL;
    }
#endif

    for (int i = 0; i < NUM_NT_SYSCALLS; i++) {
        uint16_t nr = nt_syscall_list[i];
        uint8_t *loc = (uint8_t *)thunk_blob + ((size_t)i * THUNK_SIZE);
#if defined(__i386__)
        write_thunk_at_32(loc, nr, dispatcher);
#else
        write_thunk_at(loc, nr, dispatcher);
#endif
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
