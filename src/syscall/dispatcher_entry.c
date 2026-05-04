/*
 * dispatcher_entry.c
 *
 * Manages the pre-allocated UNIX stack used during syscall dispatch.
 * The assembly dispatcher entry (dispatcher_entry.S) switches to this
 * stack before calling the C dispatcher, ensuring we do not clobber
 * the guest stack while handling the syscall.
 */

#include <stdio.h>
#include <sys/mman.h>
#include "include/syscall/dispatcher_entry.h"

#define UNIX_STACK_SIZE (128 * 1024)  /* 128 KB */

struct guest_regs __wine_guest_regs = {0};
void *unix_stack_ptr_val = NULL;

int setup_unix_stack(void)
{
    void *base = mmap(NULL, UNIX_STACK_SIZE,
                      PROT_READ | PROT_WRITE,
                      MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);

    if (base == MAP_FAILED) {
        fprintf(stderr, "wine: failed to allocate UNIX stack\n");
        return -1;
    }

    /* Set to top of the region. mmap returns page-aligned (4096),
     * and 128 KB is a multiple of 16, so base + 131072 is 16-byte aligned */
    unix_stack_ptr_val = (char *)base + UNIX_STACK_SIZE;
    return 0;
}

void cleanup_unix_stack(void)
{
    if (unix_stack_ptr_val != NULL) {
        void *base = (char *)unix_stack_ptr_val - UNIX_STACK_SIZE;
        munmap(base, UNIX_STACK_SIZE);
        unix_stack_ptr_val = NULL;
    }
}
