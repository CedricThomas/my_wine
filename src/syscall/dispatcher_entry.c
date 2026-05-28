/*
 * dispatcher_entry.c
 *
 * Manages the pre-allocated UNIX stack used during syscall dispatch.
 * The assembly dispatcher entry (dispatcher_entry_asm.S) switches to this
 * stack before calling the C dispatcher, ensuring we do not clobber
 * the guest stack while handling the syscall.
 */

#include <stdio.h>
#include <sys/mman.h>
#include "include/syscall/dispatcher_entry.h"
#include "include/debug.h"
#include "include/common.h"

/* loader_state.h provides g_loader.image_base / g_loader.image_size for
 * dynamic unix stack placement (PE32 only). */
#include "../loader/loader_state.h"

#if defined(MY_WINE32)
#include "syscalls_inline.h"
#define wine_mmap(a, l, p, f, fd, o) INLINE_SYSCALL_MMAP(a, l, p, f, fd, o)
#define wine_munmap(a, l) INLINE_SYSCALL_MUNMAP(a, l)
#else
#define wine_mmap(a, l, p, f, fd, o) mmap(a, l, p, f, fd, o)
#define wine_munmap(a, l) munmap(a, l)
#endif

#if defined(MY_WINE32)
#define UNIX_STACK_SIZE (128 * 1024)  /* placed after PE image for PE32 */
#define GUEST_STACK_BASE 0x00500000U
#define GUEST_STACK_SIZE (512 * 1024U)
#define SIG_STACK_BASE   0x00800000U
#else
#define UNIX_STACK_SIZE (2 * 1024 * 1024)
#endif

struct guest_regs __wine_guest_regs = {0};
void *unix_stack_ptr_val = NULL;

/* c_dispatch_syscall is implemented in dispatcher.c */

int setup_unix_stack(void)
{
#if defined(MY_WINE32)
    /* 32-bit: place the unix stack above the PE image to avoid overwriting
     * guest data (e.g., DOOM95 .bss extends to 0x618600, which was inside
     * the old fixed 0x00600000 slot). Compute base from image_size so this
     * works for any PE32 binary, and skip over the fixed guest stack window.
     * We intentionally avoid MAP_STACK here: for PE32 it tends to place the
     * mapping near host libc (0xf7xxxxxx), which breaks dispatch. */
    uintptr_t base_addr;
    uintptr_t guest_stack_end = GUEST_STACK_BASE + GUEST_STACK_SIZE;
    uintptr_t sig_stack_base = SIG_STACK_BASE;

    if (g_loader.image_base != NULL && g_loader.image_size != 0) {
        /* Place unix stack right after the PE image, page-aligned. */
        base_addr = (uintptr_t)g_loader.image_base + g_loader.image_size;
        base_addr = (base_addr + PAGE_MASK) & ~(uintptr_t)PAGE_MASK;

        /* Do not overlap the fixed guest stack at 0x00500000..0x00580000. */
        if (base_addr < guest_stack_end &&
            base_addr + UNIX_STACK_SIZE > GUEST_STACK_BASE) {
            base_addr = guest_stack_end;
        }
    } else {
        base_addr = 0x00600000U;
    }

    /* Ensure there's room before the fixed alt signal stack at 0x00800000. */
    if (base_addr + UNIX_STACK_SIZE > sig_stack_base) {
        DEBUG("wine: no safe low-memory slot for unix stack "
              "(base=0x%x size=0x%x)",
              (unsigned)base_addr, (unsigned)UNIX_STACK_SIZE);
        return -1;
    }

    void *base = wine_mmap((void *)base_addr, UNIX_STACK_SIZE,
                           PROT_READ | PROT_WRITE,
                           MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED, -1, 0);
#else
    void *base = wine_mmap(NULL, UNIX_STACK_SIZE,
                      PROT_READ | PROT_WRITE,
                      MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
#endif

    if (base == MAP_FAILED) {
        DEBUG("wine: failed to allocate UNIX stack");
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
        wine_munmap(base, UNIX_STACK_SIZE);
        unix_stack_ptr_val = NULL;
    }
}

/* Weak reference to the assembly dispatcher entry point.
 * Resolves to NULL (not a link error) in test builds where
 * dispatcher_entry_asm.S is not linked. */
extern __attribute__((weak)) void __wine_dispatcher(void);

/* Return the address of the assembly dispatcher for thunk generation */
void *wine_dispatcher_addr(void)
{
    return (void *)__wine_dispatcher;
}
