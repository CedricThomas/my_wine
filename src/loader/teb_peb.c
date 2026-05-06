/*
 * teb_peb.c — TEB/PEB setup and guest stack allocation
 *
 * Allocates and initializes the Thread Environment Block (TEB),
 * Process Environment Block (PEB), and guest stack.
 */

#define _GNU_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>
#include <sys/mman.h>

#include "include/pe.h"
#include "include/nt_constants.h"
#include "loader_priv.h"
#include "include/debug.h"
#include "../heap/wine_heap.h"
#include "peb_ldr.h"
#include "module_list.h"
#include "include/common.h"

void *g_stack_base = NULL;
size_t g_stack_size = 0;

/**
 * Set up the TEB (Thread Environment Block) and PEB (Process Environment Block).
 *
 * Allocates both structures via mmap, initializes the SEH chain,
 * self-referential pointers, and links PEB into TEB at offset 0x60.
 * Sets GS base to point to the TEB.
 *
 * @return TEB pointer, or NULL on failure.
 */
void *setup_teb_peb(void)
{
    /* Allocate TEB (Thread Environment Block) - at least 4KB */
    size_t teb_size = PAGE_SIZE;
    void *teb = mmap(NULL, teb_size, PROT_READ|PROT_WRITE,
                      MAP_PRIVATE|MAP_ANONYMOUS|MAP_STACK, -1, 0);
    if (teb == MAP_FAILED) {
        perror("mmap TEB");
        return NULL;
    }

    /* Zero the TEB */
    memset(teb, 0, teb_size);

    /* SEH chain (gs:[0x00]) is set up by the child in entry.c */

    /* Fix gs:[0x30] null deref crash at PE entry point:
     *   Guest code reads TEB from gs:[0x30], then follows
     *   the self-referential pointer at teb[0x08] to verify.
     *   Without this, the bootstrap loop (rsi==rax check) never exits. */
    *(void **)((uint8_t *)teb + TEB_TEB_SELF_REF) = teb;  // TEB self-referential
    *(void **)((uint8_t *)teb + TEB_THREAD_PTR) = teb;  // fake thread pointer (self-ref)

    /* Allocate PEB (Process Environment Block) */
    size_t peb_size = PAGE_SIZE;
    void *peb = mmap(NULL, peb_size, PROT_READ|PROT_WRITE,
                      MAP_PRIVATE|MAP_ANONYMOUS|MAP_STACK, -1, 0);
    if (peb == MAP_FAILED) {
        perror("mmap PEB");
        munmap(teb, teb_size);
        return NULL;
    }

    memset(peb, 0, peb_size);

    /* Set PEB pointer in TEB at offset TEB_PEB_PTR */
    *(void **)((char *)teb + TEB_PEB_PTR) = peb;

    /* Set image base pointer in PEB at offset PEB_IMAGE_BASE (ImageBaseAddress) */
    *(void **)((char *)peb + PEB_IMAGE_BASE) = g_image_base;

    /* Set BeingDebugged = 0 in PEB at offset PEB_BEING_DEBUGGED */
    *(uint8_t *)((char *)peb + PEB_BEING_DEBUGGED) = 0;

    /* Initialize process heap and store in PEB at PEB_PROCESS_HEAP */
    void *ph = init_process_heap();
    *(void **)((char *)peb + PEB_PROCESS_HEAP) = ph;

    /* Initialize module registry and PEB LDR */
    init_module_list();
    g_peb_ldr = init_peb_ldr();
    if (g_peb_ldr != NULL) {
        /* Set PEB[PEB_LDR = 0x18] to point to LDR data */
        *(void **)((char *)peb + PEB_LDR) = g_peb_ldr;

        /* Register the main PE as the first module */
        if (g_image_base != NULL) {
            /* Verify g_image_base is a real mapped PE image before dereferencing.
             * madvise returns -ENONET for unmapped addresses, which guards against
             * test scenarios where g_image_base is set to a fake value. */
            if (madvise(g_image_base, 1, MADV_NORMAL) == 0) {
                IMAGE_DOS_HEADER *img_dos = (IMAGE_DOS_HEADER *)g_image_base;
                IMAGE_NT_HEADERS64 *img_nt = (IMAGE_NT_HEADERS64 *)((char *)g_image_base + img_dos->e_lfanew);
                loaded_module_t *mod = add_module(g_image_base, "main.exe", img_nt);
                if (mod != NULL) {
                    ldr_add_module(mod);
                }
            }
        }
    }

    /*
     * Do NOT set GS base here. The GS base should remain pointing to
     * Linux TLS for all glibc calls during setup. GS base is set to the
     * TEB in setup_guest_state() right before jumping to guest code.
     * Setting it here would corrupt glibc TLS access (sigaction, mmap,
     * etc.) because glibc reads TLS via GS-relative accesses.
     */

    DEBUG("TEB at %p, PEB at %p", teb, peb);

    return teb;
}

/**
 * Allocate the guest stack according to the PE's SizeOfStackReserve
 * and SizeOfStackCommit fields (with sensible minimums).
 *
 * @param  opt  pointer to the PE's IMAGE_OPTIONAL_HEADER64
 * @return  top-of-stack pointer (aligned for x86_64 ABI), or NULL
 */
void *setup_stack(IMAGE_OPTIONAL_HEADER64 *opt)
{
    uint64_t reserve = opt->SizeOfStackReserve;
    uint64_t commit  = opt->SizeOfStackCommit;

    /* Ensure minimum sizes */
    if (reserve == 0) reserve = 1024 * 1024; /* 1MB default */
    if (commit  == 0) commit   = PAGE_SIZE;   /* 1 page minimum */

    /* CRITICAL: ensure at least 512KB of stack for CRT startup (mainCRTStartup
     * needs significant stack for nested calls to __getmainargs, _initterm, etc.)
     * The PE header often specifies only 4KB commit, which is insufficient. */
    if (commit < 512 * 1024) commit = 512 * 1024;

    /* Align to page boundary */
    reserve = (reserve + PAGE_MASK) & ~(uint64_t)PAGE_MASK;
    commit  = (commit  + PAGE_MASK) & ~(uint64_t)PAGE_MASK;

    /* Allocate stack (grows downward on x86_64) */
    void *stack_base = mmap(NULL, commit, PROT_READ|PROT_WRITE,
                             MAP_PRIVATE|MAP_ANONYMOUS|MAP_STACK, -1, 0);
    if (stack_base == MAP_FAILED) {
        perror("mmap stack");
        return NULL;
    }

    /* Top of stack (aligned to 16 bytes for x86_64 ABI requirement).
     * Sub 8 BEFORE alignment to ensure stack_top - 8 never exceeds
     * the mmap'd region. Without this, page-aligned bases + page-aligned
     * commits can push stack_top past the region boundary under ASLR. */
    uintptr_t stack_top = (uintptr_t)stack_base + commit;
    stack_top = ((stack_top - 8) & ~(uintptr_t)15) + 8;  /* ABI requires rsp%16==8 */


    /* Print stack info */
    DEBUG("Stack: base=%p, top=%p, reserve=0x%lx, commit=0x%lx",
           stack_base, (void *)stack_top,
           (unsigned long)reserve, (unsigned long)commit);

    /* Store stack_base at a known location for later use */
    *(void **)((uintptr_t)stack_top - 8) = stack_base;
    g_stack_base = stack_base;
    g_stack_size = commit;
    return (void *)stack_top;
}
