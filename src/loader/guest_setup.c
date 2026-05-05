/*
 * guest_setup.c — Guest state, patches, .bss mprotect
 *
 * Contains all guest setup logic that runs in the single-process model:
 * - SEH chain + syscall thunk generation
 * - Guest state (GS base, TEB SEH chain, PE re-parse)
 * - __acrt_iob_func patching
 * - .bss section mprotect
 * - Unix stack setup for syscall dispatch
 * - Cleanup of guest resources (TEB, PEB, stack, thunk pages)
 *
 * setup_guest_and_run and cleanup_guest are called from entry.c.
 */

#define _GNU_SOURCE

#include "../syscalls_inline.h"
#include "loader_priv.h"
#include "include/pe.h"
#include "include/msvcrt.h"
#include "include/nt_constants.h"
#include "include/syscall/thunk_gen.h"
#include "include/syscall/dispatcher.h"
#include "include/syscall/dispatcher_entry.h"
#include "include/common.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>
#include <sys/mman.h>
#include <signal.h>
#include <sys/syscall.h>
#include <fcntl.h>
#include <stdbool.h>
#include "include/debug.h"

/* ── Extern declarations ─────────────────────────────────────── */

/* Guest entry trampoline (implemented in run_guest.S) */
extern void run_guest(void (*)(void), void *, void *, char **, char **,
                       void (*)(uint32_t)) __attribute__((noreturn));

/* Accessor for __wine_iob_data — used for patching __acrt_iob_func */
extern void *__wine_iob_data(void);

/* From crash_handlers.c — installed before we do anything else */
extern void setup_signal_handlers(void);

/* SEH crash handler (defined in crash_handlers.c) — wired into SEH frame */
extern void seh_crash_handler(void *, void *, void *, void *);

/* ── __acrt_iob_func patching ────────────────────────────────── */

/**
 * Patch __acrt_iob_func to return __wine_iob_data directly.
 *
 * WHY: The PE's __acrt_iob_func (msvcrt import wrapper) does:
 *   1. mov %ecx,%ebx     ; save index
 *   2. call __iob_func   ; our stub (ms_abi) may clobber rcx upper bits
 *   3. mov %ebx,%ecx     ; restore lower 32 bits (upper 32 remain garbage)
 *   4. lea (%rcx,%rcx,2),%rdx  ; rdx = rcx*3 (GARBAGE from upper bits)
 *   5. shl $4,%rdx       ; rdx *= 16 = rcx*48 (still GARBAGE)
 *   6. add %rdx,%rax     ; rax = base + garbage → wrong FILE*
 *
 * We cannot fix this from __iob_func alone because the garbage is in
 * rcx which our function doesn't control. Patching the wrapper to return
 * the base directly (bypassing the broken index math) is the cleanest fix.
 *
 * Patch: movabs $<addr>,%rax; ret; 4x NOP (15 bytes total)
 * This replaces the jmp thunk or wrapper with a direct return.
 */

/* Callback for with_mprotect_rw in patch_acrt_iob */
static void acrt_iob_patch_cb(void *arg)
{
    uint8_t *code = (uint8_t *)arg;
    code[0] = X86_REX_W;                /* REX.W */
    code[1] = X86_MOV_ABS;                /* movabs rax, imm64 */
    *(uint64_t *)(code + 2) = (uint64_t)(uintptr_t)__wine_iob_data();
    code[10] = X86_RET;
    for (int k = 11; k < 15; k++) code[k] = X86_NOP;
}

/* Compute .text section end for bounds checking */
static uint64_t find_text_end(IMAGE_NT_HEADERS64 *nt, IMAGE_SECTION_HEADER *sections)
{
    for (uint16_t i = 0; i < nt->FileHeader.NumberOfSections; i++) {
        if (memcmp(sections[i].Name, ".text", 5) == 0) {
            uint64_t end = sections[i].VirtualAddress + sections[i].Misc.VirtualSize;
            if (end < sections[i].VirtualAddress ||
                sections[i].SizeOfRawData > sections[i].Misc.VirtualSize)
                end = sections[i].VirtualAddress + sections[i].SizeOfRawData;
            return end;
        }
    }
    return 0;
}

/* Validate opcode, check bounds, apply the iob patch */
static int apply_iob_patch(void *thunk, uint8_t *code, uint64_t thunk_off,
                           uint64_t text_end)
{
    /* Validate: first two bytes should be ff 25 (jmp *disp32(%rip)) */
    if (code[0] != X86_JMP_RIP || code[1] != X86_MOD_RIP) {
        fprintf(stderr, "WARNING: __acrt_iob_func at 0x%lx has unexpected opcode 0x%02x 0x%02x, skipping patch\n",
                (unsigned long)(uintptr_t)thunk, code[0], code[1]);
        return -1;
    }

    /* Bounds check: ensure 15-byte patch won't exceed .text section */
    if (text_end == 0 || thunk_off + 15 > text_end) {
        fprintf(stderr, "WARNING: __acrt_iob_func thunk at 0x%lx is too close to .text end (need 15 bytes, have %ld), skipping patch\n",
                (unsigned long)thunk_off, (long)(text_end > thunk_off ? text_end - thunk_off : 0));
        return -1;
    }

    if (with_mprotect_rw(thunk, 15, acrt_iob_patch_cb, thunk, PROT_READ | PROT_EXEC) != 0) {
        perror("mprotect __acrt_iob_func");
        return -1;
    }
    return 0;
}

static void patch_acrt_iob(void *base, IMAGE_NT_HEADERS64 *nt,
                           IMAGE_SECTION_HEADER *sections)
{
    uint64_t text_end = find_text_end(nt, sections);

    /*
     * Find the .text jmp-thunk whose IAT target resolves to __iob_func.
     * Use find_text_thunk() to scan "ff 25 disp32" instructions and check
     * the dereferenced IAT pointer.
     */
    void *thunk = find_text_thunk(base, nt, sections, __iob_func);

    if (thunk == NULL) {
        fprintf(stderr, "WARNING: __acrt_iob_func thunk not found, skipping patch\n");
    } else {
        uint8_t *code = (uint8_t *)thunk;
        uint64_t thunk_off = (uint64_t)thunk - (uint64_t)base;
        apply_iob_patch(thunk, code, thunk_off, text_end);
    }
}

/* ── Step 2: SEH chain + syscall thunks ──────────────────────── */

/**
 * Set up the SEH chain and generate syscall thunks.
 * Also sets up the unix stack for syscall dispatch.
 *
 * Creates a static SEH frame and generates all syscall thunks.
 *
 * @return  pointer to the static SEH frame for TEB wiring
 */
static void *setup_seh_and_thunks(void)
{
    /* SEH chain (frame must persist for guest SEH walk) */
    static __attribute__((aligned(8))) uint64_t guest_seh_frame[2];
    guest_seh_frame[0] = 0;  /* next = NULL (end of chain) */
    guest_seh_frame[1] = (uint64_t)(uintptr_t)&seh_crash_handler;

    generate_all_thunks();

    /* Set up unix stack for syscall dispatch */
    if (setup_unix_stack() != 0) {
        fprintf(stderr, "my_wine: cannot setup unix stack, aborting\n");
        _exit(1);
    }

    return guest_seh_frame;
}

/* ── Step 3a: PE header re-parse ── */

static void parse_pe_headers(uint64_t entry_abs,
                             IMAGE_NT_HEADERS64 **out_nt,
                             IMAGE_SECTION_HEADER **out_sections)
{
    /* Re-parse PE headers from entry_abs to get nt_headers + sections */
    uint64_t image_base = entry_abs & ~0xFFFFFUL;
    void *base = (void *)(uintptr_t)image_base;
    const IMAGE_DOS_HEADER *img_dos = (const IMAGE_DOS_HEADER *)base;
    uint32_t pe_off = img_dos->e_lfanew;
    IMAGE_NT_HEADERS64 *nt = (IMAGE_NT_HEADERS64 *)((char *)base + pe_off);
    uint32_t sec_off = pe_off + sizeof(uint32_t) + sizeof(IMAGE_FILE_HEADER) +
                       nt->FileHeader.SizeOfOptionalHeader;
    IMAGE_SECTION_HEADER *sections =
        (IMAGE_SECTION_HEADER *)((char *)base + sec_off);

    *out_nt = nt;
    *out_sections = sections;
}

/* ── Step 3b: Finalize guest state (GS base + TEB SEH) ── */

static void finalize_guest_state(void *teb, void *seh_frame)
{
    /* Re-set GS base */
    if (set_gs_base(teb) != 0) {
        fprintf(stderr, "my_wine: cannot set GS base, aborting\n");
        _exit(1);
    }

    /* Point TEB gs:[0x00] to our SEH frame */
    *(void **)((uint8_t *)teb + TEB_SEH_CHAIN) = seh_frame;
}

/* ── Step 4: Final patches ───────────────────────────────────── */

static void apply_final_patches(void *base, IMAGE_NT_HEADERS64 *nt,
                                IMAGE_SECTION_HEADER *sections)
{
    patch_acrt_iob(base, nt, sections);

    /* Ensure .bss is writable (mprotect may not propagate) */
    for (uint32_t i = 0; i < nt->FileHeader.NumberOfSections; i++) {
        if (sections[i].Characteristics & IMAGE_SCN_MEM_WRITE) {
            size_t sz = sections[i].Misc.VirtualSize;
            if (sz == 0) sz = sections[i].SizeOfRawData;
            if (sz == 0) continue;
            sz = (sz + PAGE_MASK) & ~(size_t)PAGE_MASK;
            uintptr_t addr = (uintptr_t)base + sections[i].VirtualAddress;
            if (mprotect((void *)addr, sz, PROT_READ|PROT_WRITE) != 0) {
                fprintf(stderr, "WARNING: mprotect write section '%.8s' at 0x%lx failed\n",
                        sections[i].Name, (unsigned long)addr);
            }
        }
    }
}

/* ── Step 5: Jump to guest (noreturn) ───────────────────────── */
static __attribute__((noreturn)) void jump_to_guest(uint64_t entry_abs, void *stack_top,
                                                    char **guest_argv, char **guest_envp)
{
    void (*entry)(void) = (void (*)(void))(void *)(uintptr_t)entry_abs;

    /* Find ExitProcess from the import table so you can call it after main returns */
    void (*exit_fn)(uint32_t) = NULL;
    for (int i = 0; import_table[i].name != NULL; i++) {
        if (strcmp(import_table[i].name, "ExitProcess") == 0 &&
            import_table[i].address != NULL) {
            exit_fn = (void (*)(uint32_t))import_table[i].address;
            break;
        }
    }
    if (!exit_fn) {
        fprintf(stderr, "ERROR: ExitProcess not found in import table\n");
        _exit(1);
    }
    /* ExitProcess found */

    run_guest(entry, stack_top, NULL, guest_argv, guest_envp, exit_fn);

    /* run_guest should not return (ExitProcess exits) */
    _exit(1);
}

/* ── Orchestrator ────────────────────────────────────────────── */

/**
 * Set up the guest and jump to the PE entry point.
 * Called in the single-process model; does not return.
 */
__attribute__((noreturn)) void setup_guest_and_run(
        uint64_t entry_abs, void *stack_top, void *teb,
        char **guest_argv, char **guest_envp)
{
    setup_signal_handlers();
    void *seh_frame = setup_seh_and_thunks();

    DEBUG("my_wine: jumping to entry 0x%lx via inline asm",
          (unsigned long)entry_abs);

    IMAGE_NT_HEADERS64 *nt = NULL;
    IMAGE_SECTION_HEADER *sections = NULL;

    parse_pe_headers(entry_abs, &nt, &sections);

    uint64_t image_base = entry_abs & ~0xFFFFFUL;
    void *base = (void *)(uintptr_t)image_base;
    apply_final_patches(base, nt, sections);

    finalize_guest_state(teb, seh_frame);

    jump_to_guest(entry_abs, stack_top, guest_argv, guest_envp);
}

/**
 * cleanup_guest — unmap all guest resources (TEB, PEB, stack, thunk pages).
 *
 * Called after the guest exits (in NtTerminateProcess) to reclaim mmap'd memory.
 *
 * @param  teb        TEB pointer (NULL to skip TEB/PEB cleanup)
 * @param  stack_base guest stack base (NULL to skip stack cleanup)
 */
void cleanup_guest(void *teb, void *stack_base)
{
    if (teb) {
        /* Unmap PEB first (it's separate from TEB, stored at teb+0x60) */
        void *peb = *(void **)((char *)teb + TEB_PEB_PTR);
        if (peb) {
            if (munmap(peb, PAGE_SIZE) != 0) {
                perror("cleanup_guest: munmap PEB");
            }
        }
        /* Unmap TEB */
        if (munmap(teb, PAGE_SIZE) != 0) {
            perror("cleanup_guest: munmap TEB");
        }
    }

    if (stack_base && g_stack_size > 0) {
        if (munmap(stack_base, g_stack_size) != 0) {
            perror("cleanup_guest: munmap stack");
        }
    }

    /* Unmap thunk pages (from thunk_gen.c) */
    cleanup_thunk_pages();
}
