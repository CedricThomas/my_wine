/*
 * gs_base.c — GS base set/get helpers with FSGSBASE fallback
 *
 * Some environments silently ignore arch_prctl(ARCH_SET_GS) while
 * returning 0.  This module tries arch_prctl first, verifies the
 * result, and falls back to the wrgsbase/rdgsbase instructions when
 * needed.
 *
 * Must be compiled with -mno-red-zone (part of the loader).
 */

#define _GNU_SOURCE

#include <sys/syscall.h>
#include <asm/prctl.h>
#include <unistd.h>

#include "include/debug.h"

/**
 * set_gs_base — set the GS segment base to addr.
 *
 * Strategy:
 *   1. arch_prctl(ARCH_SET_GS)
 *   2. Verify with arch_prctl(ARCH_GET_GS); if mismatch, try FSGSBASE
 *
 * @param  addr  desired GS base address
 * @return 0 on success, -1 on failure
 */
int set_gs_base(void *addr)
{
    long rc;

    /* Try arch_prctl SET */
    rc = syscall(__NR_arch_prctl, ARCH_SET_GS, (unsigned long)addr);
    if (rc != 0) {
        DEBUG("my_wine: arch_prctl(ARCH_SET_GS) failed, rc=%ld", rc);
        goto fallback;
    }

    /* Verify with arch_prctl GET */
    long got = syscall(__NR_arch_prctl, ARCH_GET_GS, 0);
    if (got < 0) {
        goto fallback;
    }

    if ((void *)got != addr) {
        /* arch_prctl SET returned 0 but value mismatch — silent
           failure detected; fall through to FSGSBASE fallback */
        goto fallback;
    }

    return 0;  /* verified OK */

fallback:
    /* Fallback: write GS base directly with FSGSBASE instruction */
    __asm__ volatile ("wrgsbase %0" :: "r"((unsigned long)addr));

    /* Verify the fallback write */
    unsigned long val;
    __asm__ volatile ("rdgsbase %0" : "=r"(val));
    if ((void *)val == addr) {
        return 0;
    }
    return -1;
}

/**
 * get_gs_base — read the current GS segment base.
 *
 * Tries arch_prctl first; falls back to rdgsbase on failure.
 *
 * @return  current GS base address, or NULL if both methods fail
 */
void *get_gs_base(void)
{
    long rc;

    /* Try arch_prctl GET */
    rc = syscall(__NR_arch_prctl, ARCH_GET_GS, 0);
    if (rc >= 0) {
        return (void *)rc;
    }

    /* Fallback: rdgsbase — no DEBUG, GS may point to TEB */
    unsigned long val;
    __asm__ volatile ("rdgsbase %0" : "=r"(val));
    return (void *)val;
}
