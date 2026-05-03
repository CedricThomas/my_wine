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
#include <sys/syscall.h>
#include <asm/prctl.h>

#include "include/pe.h"
#include "loader_priv.h"

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
    size_t teb_size = 4096;
    void *teb = mmap(NULL, teb_size, PROT_READ|PROT_WRITE,
                      MAP_PRIVATE|MAP_ANONYMOUS|MAP_STACK, -1, 0);
    if (teb == MAP_FAILED) {
        perror("mmap TEB");
        return NULL;
    }

    /* Zero the TEB */
    memset(teb, 0, teb_size);

    /* SEH chain (gs:[0x00]) is set up by the child in entry.c */

    /* Fix gs:[0x30] null deref crash at 0x1400011d4:
     *   mov rax, gs:[0x30]  →  rax must be TEB
     *   mov rsi, [rax+8]    →  teb[0x08] must be TEB (self-ref)
     * so the loop that checks rsi==rax can exit. */
    *(void **)((uint8_t *)teb + 0x08) = teb;  // TEB self-referential
    *(void **)((uint8_t *)teb + 0x30) = teb;  // fake thread pointer (self-ref)

    /* Allocate PEB (Process Environment Block) */
    size_t peb_size = 4096;
    void *peb = mmap(NULL, peb_size, PROT_READ|PROT_WRITE,
                      MAP_PRIVATE|MAP_ANONYMOUS|MAP_STACK, -1, 0);
    if (peb == MAP_FAILED) {
        perror("mmap PEB");
        munmap(teb, teb_size);
        return NULL;
    }

    memset(peb, 0, peb_size);

    /* Set PEB pointer in TEB at offset 0x60 */
    *(void **)((char *)teb + 0x60) = peb;

    /* Set image base pointer in PEB at offset 0x008 (ImageBaseAddress) */
    *(void **)((char *)peb + 0x008) = g_image_base;

    /* Set BeingDebugged = 0 in PEB at offset 0x002 */
    *(uint8_t *)((char *)peb + 0x002) = 0;

    /* Set GS segment to point to TEB */
    if (syscall(__NR_arch_prctl, ARCH_SET_GS, (unsigned long)teb) != 0) {
        perror("arch_prctl ARCH_SET_GS");
        munmap(peb, peb_size);
        munmap(teb, teb_size);
        return NULL;
    }

    printf("TEB at %p, PEB at %p\n", teb, peb);
    printf("arch_prctl(ARCH_GET_GS) = %p\n",
           (void *)syscall(__NR_arch_prctl, ARCH_GET_GS, 0));

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
    if (commit  == 0) commit   = 4096;        /* 1 page minimum */

    /* CRITICAL: ensure at least 512KB of stack for CRT startup (mainCRTStartup
     * needs significant stack for nested calls to __getmainargs, _initterm, etc.)
     * The PE header often specifies only 4KB commit, which is insufficient. */
    if (commit < 512 * 1024) commit = 512 * 1024;

    /* Align to page boundary */
    reserve = (reserve + 4095) & ~(uint64_t)4095;
    commit  = (commit  + 4095) & ~(uint64_t)4095;

    /* Allocate stack (grows downward on x86_64) */
    void *stack_base = mmap(NULL, commit, PROT_READ|PROT_WRITE,
                             MAP_PRIVATE|MAP_ANONYMOUS|MAP_STACK, -1, 0);
    if (stack_base == MAP_FAILED) {
        perror("mmap stack");
        return NULL;
    }

    /* Top of stack (aligned to 16 bytes for x86_64 ABI requirement) */
    uintptr_t stack_top = (uintptr_t)stack_base + commit;
    stack_top = (stack_top & ~(uintptr_t)15) + 8;  /* ABI requires rsp%16==8 */


    /* Print stack info */
    printf("Stack: base=%p, top=%p, reserve=0x%lx, commit=0x%lx\n",
           stack_base, (void *)stack_top,
           (unsigned long)reserve, (unsigned long)commit);

    /* Store stack_base at a known location for later use */
    *(void **)((uintptr_t)stack_top - 8) = stack_base;
    g_stack_base = stack_base;
    g_stack_size = commit;
    return (void *)stack_top;
}
