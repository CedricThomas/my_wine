/*
 * teb_peb.c — TEB/PEB setup and guest stack allocation
 *
 * Allocates and initializes the Thread Environment Block (TEB),
 * Process Environment Block (PEB), and guest stack.
 *
 * Supports both PE32 (32-bit) and PE32+ (64-bit) images.
 * For PE32, TEB/PEB/stack are placed below 4GB with 4-byte pointer writes.
 */

#define _GNU_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>
#include <sys/mman.h>

#if defined(MY_WINE_32)
#include "../syscall/syscalls_inline.h"
/* Standalone 32-bit: use inline syscalls instead of libc */
#define wine_mmap(a, l, p, f, fd, o) INLINE_SYSCALL_MMAP(a, l, p, f, fd, o)
#define wine_munmap(a, l) INLINE_SYSCALL_MUNMAP(a, l)
#define wine_mprotect(a, l, p) INLINE_SYSCALL_MPROTECT(a, l, p)
#define wine_log_error(msg) do { const char *_perr[] = { msg, ": " }; \
    INLINE_SYSCALL_WRITE_ERR(_perr[0], __builtin_strlen(_perr[0])); } while(0)
#else
#define wine_mmap(a, l, p, f, fd, o) mmap(a, l, p, f, fd, o)
#define wine_munmap(a, l) munmap(a, l)
#define wine_mprotect(a, l, p) mprotect(a, l, p)
#define wine_log_error(msg) perror(msg)
#endif

#include "teb_peb.h"
#include "include/pe.h"
#include "include/pe_priv.h"
#include "include/nt_constants.h"
#include "loader_priv.h"
#include "include/debug.h"
#include "../heap/wine_heap.h"
#include "peb_ldr.h"
#include "module_list.h"
#include "include/common.h"

/**
 * init_teb32_fields — set all PE32 TEB fields.
 * Shared between pe32_entry.c and setup_teb_peb().
 */
void init_teb32_fields(void *teb, void *peb)
{
    uint32_t *p;

    /* TEB self-reference (TEB32_TEB_SELF_REF = 0x04) */
    p = (uint32_t *)((uint8_t *)teb + TEB32_TEB_SELF_REF);
    *p = (uint32_t)(uintptr_t)teb;

    /* ThreadPointer (TEB32_THREAD_PTR = 0x24) */
    p = (uint32_t *)((uint8_t *)teb + TEB32_THREAD_PTR);
    *p = (uint32_t)(uintptr_t)teb;

    /* FiberData (TEB32_FIBER_DATA = 0x10) */
    p = (uint32_t *)((uint8_t *)teb + TEB32_FIBER_DATA);
    *p = (uint32_t)(uintptr_t)teb;

    /* LastStatusValue (TEB+0x34) = STATUS_SUCCESS (0) */
    p = (uint32_t *)((uint8_t *)teb + 0x34);
    *p = 0;

    /* GDI data targets (both zero) */
    p = (uint32_t *)((uint8_t *)teb + TEB32_GDI_PROCESS_LOCAL);
    p[0] = 0;  /* GdiProcessLocals */
    p[1] = 0;  /* GdiThreadLocals */

    /* GdiTebOffset (TEB32_GDI_TEB_OFFSET = 0x18) — CRT bootstrap reads fs:[0x18] */
    p = (uint32_t *)((uint8_t *)teb + TEB32_GDI_TEB_OFFSET);
    *p = (uint32_t)(uintptr_t)teb + TEB32_GDI_PROCESS_LOCAL;

    /* Wire PEB pointer into TEB (TEB32_PEB_PTR = 0x30) */
    if (peb) {
        p = (uint32_t *)((uint8_t *)teb + TEB32_PEB_PTR);
        *p = (uint32_t)(uintptr_t)peb;
    }
}

/**
 * init_peb32_fields — set all PE32 PEB fields.
 * Shared between pe32_entry.c and setup_teb_peb().
 */
void init_peb32_fields(void *peb, void *image_base)
{
    /* PEB->BeingDebugged (PEB32_BEING_DEBUGGED = 0x002) */
    *(uint8_t *)((uint8_t *)peb + PEB32_BEING_DEBUGGED) = 0;

    /* PEB->ImageBaseAddress (PEB32_IMAGE_BASE = 0x008) */
    *(uint32_t *)((uint8_t *)peb + PEB32_IMAGE_BASE) =
        (uint32_t)(uintptr_t)image_base;
}

/**
 * write_guest_ptr — write a guest pointer (4 bytes for PE32, 8 for PE64) at
 *                    offset bytes into base.
 */
static inline void write_guest_ptr(void *base, size_t offset, void *value)
{
    if (g_is_32bit) {
        *(uint32_t *)((char *)base + offset) = (uint32_t)(uintptr_t)value;
    } else {
        *(void **)((char *)base + offset) = value;
    }
}

/**
 * write_guest_u8 — write a single byte at offset bytes into base.
 */
static inline void write_guest_u8(void *base, size_t offset, uint8_t value)
{
    *(uint8_t *)((char *)base + offset) = value;
}


void *g_stack_base = NULL;
size_t g_stack_size = 0;

/**
 * alloc_teb — allocate and zero a TEB page, remap below 4GB for PE32.
 *
 * @param  teb_size  allocation size (typically PAGE_SIZE)
 * @return  TEB pointer, or NULL on failure.
 */
static void *alloc_teb(size_t teb_size)
{
    void *teb = wine_mmap(NULL, teb_size, PROT_READ|PROT_WRITE,
                   MAP_PRIVATE|MAP_ANONYMOUS|MAP_STACK, -1, 0);
    if (teb == MAP_FAILED) {
        wine_log_error("mmap TEB");
        return NULL;
    }

#if __SIZEOF_POINTER__ == 8
    /* For PE32, TEB must be below 4GB so the guest can address it with 32-bit pointers */
    if (g_is_32bit && (uintptr_t)teb >= ADDR32_LIMIT) {
        DEBUG("TEB at %p above 4GB for PE32, remapping to 0x%08X", teb, TEB32_FIXED_ADDR);
        if (wine_munmap(teb, teb_size) != 0) {
            wine_log_error("munmap TEB before remap");
            return NULL;
        }
        teb = wine_mmap((void *)TEB32_FIXED_ADDR, teb_size, PROT_READ|PROT_WRITE,
                    MAP_PRIVATE|MAP_ANONYMOUS|MAP_STACK|MAP_FIXED, -1, 0);
        if (teb == MAP_FAILED) {
            wine_log_error("mmap TEB (MAP_FIXED)");
            return NULL;
        }
    }
#endif

    /* Zero the TEB */
    memset(teb, 0, teb_size);

    /* SEH chain (gs:[0x00]) is set up by the child in entry.c */

    /* Fix gs:[0x48] null deref crash at PE entry point:
     *   Guest code reads TEB from gs:[0x48], then follows
     *   the self-referential pointer at teb[0x08] (64) / teb[0x04] (32) to verify.
     *   Without this, the bootstrap loop (rsi==rax check) never exits. */
    if (g_is_32bit) {
        init_teb32_fields(teb, NULL);
    } else {
        write_guest_ptr(teb, TEB64_TEB_SELF_REF, teb);
        write_guest_ptr(teb, TEB64_THREAD_PTR, teb);
    }

    return teb;
}

/**
 * alloc_peb — allocate and zero a PEB page, remap below 4GB for PE32.
 *
 * @param  peb_size  allocation size (typically PAGE_SIZE)
 * @return  PEB pointer, or NULL on failure.
 */
static void *alloc_peb(size_t peb_size)
{
    void *peb = wine_mmap(NULL, peb_size, PROT_READ|PROT_WRITE,
                      MAP_PRIVATE|MAP_ANONYMOUS|MAP_STACK, -1, 0);
    if (peb == MAP_FAILED) {
        wine_log_error("mmap PEB");
        return NULL;
    }

#if __SIZEOF_POINTER__ == 8
    /* For PE32, PEB must be below 4GB */
    if (g_is_32bit && (uintptr_t)peb >= ADDR32_LIMIT) {
        DEBUG("PEB at %p above 4GB for PE32, remapping to 0x%08X", peb, PEB32_FIXED_ADDR);
        if (wine_munmap(peb, peb_size) != 0) {
            wine_log_error("munmap PEB before remap");
            return NULL;
        }
        peb = wine_mmap((void *)PEB32_FIXED_ADDR, peb_size, PROT_READ|PROT_WRITE,
                    MAP_PRIVATE|MAP_ANONYMOUS|MAP_STACK|MAP_FIXED, -1, 0);
        if (peb == MAP_FAILED) {
            wine_log_error("mmap PEB (MAP_FIXED)");
            return NULL;
        }
    }
#endif

    memset(peb, 0, peb_size);
    return peb;
}

/**
 * wire_peb_fields — connect TEB to PEB, initialize PEB fields, process heap,
 *                   module registry, and PEB LDR.
 *
 * @param  teb  TEB pointer (already allocated and zeroed)
 * @param  peb  PEB pointer (already allocated and zeroed)
 * @return  0 on success, -1 on failure.
 */
static int wire_peb_fields(void *teb, void *peb)
{
    /* Set PEB pointer in TEB */
    if (g_is_32bit) {
        init_teb32_fields(teb, peb);  /* sets PEB pointer (idempotent for other fields) */
    } else {
        write_guest_ptr(teb, TEB64_PEB_PTR, peb);
    }

    /* Set PEB fields */
    if (g_is_32bit) {
        init_peb32_fields(peb, g_image_base);
    } else {
        write_guest_ptr(peb, PEB64_IMAGE_BASE, g_image_base);
        write_guest_u8(peb, PEB64_BEING_DEBUGGED, 0);
    }

    /* Initialize process heap */
    void *ph = init_process_heap();
#if __SIZEOF_POINTER__ == 8
    if (g_is_32bit && (uintptr_t)ph >= ADDR32_LIMIT) {
        fprintf(stderr, "FATAL: process heap at %p is above 4GB for PE32 image\n", ph);
        return -1;
    }
#endif
    write_guest_ptr(peb, g_is_32bit ? PEB32_PROCESS_HEAP : PEB64_PROCESS_HEAP, ph);

    /* Initialize module registry and PEB LDR */
    init_module_list();
    g_peb_ldr = init_peb_ldr();
    if (g_peb_ldr != NULL) {
        /* Set PEB LDR pointer */
#if __SIZEOF_POINTER__ == 8
        if (g_is_32bit && (uintptr_t)g_peb_ldr >= ADDR32_LIMIT) {
            fprintf(stderr, "FATAL: PEB LDR at %p is above 4GB for PE32 image\n", g_peb_ldr);
            return -1;
        }
#endif
        write_guest_ptr(peb, g_is_32bit ? PEB32_LDR : PEB64_LDR, g_peb_ldr);

        /* Register the main PE as the first module */
        if (g_image_base != NULL) {
            /* Verify g_image_base is a real mapped PE image before dereferencing.
             * madvise returns -ENONET for unmapped addresses, which guards against
             * test scenarios where g_image_base is set to a fake value. */
            if (madvise(g_image_base, 1, MADV_NORMAL) == 0) {
                IMAGE_DOS_HEADER *img_dos = (IMAGE_DOS_HEADER *)g_image_base;
                uint32_t pe_off = img_dos->e_lfanew;
                IMAGE_NT_HEADERS img_nt;
                {
                    uint32_t opt_off = pe_off + sizeof(uint32_t) + sizeof(IMAGE_FILE_HEADER);
                    const uint16_t *magic = (const uint16_t *)((char *)g_image_base + opt_off);
                    if (*magic == IMAGE_NT_OPTIONAL_HDR32_MAGIC) {
                        img_nt.pe_type = PE_TYPE_32;
                        memcpy(&img_nt.u.nt32, (char *)g_image_base + pe_off, sizeof(IMAGE_NT_HEADERS32));
                    } else {
                        img_nt.pe_type = PE_TYPE_64;
                        memcpy(&img_nt.u.nt64, (char *)g_image_base + pe_off, sizeof(IMAGE_NT_HEADERS64));
                    }
                }
                loaded_module_t *mod = add_module(g_image_base, "main.exe", &img_nt);
                if (mod != NULL) {
                    ldr_add_module(mod);
                }
            }
        }
    }

    return 0;
}

/**
 * Set up the TEB (Thread Environment Block) and PEB (Process Environment Block).
 *
 * Thin coordinator: delegates to alloc_teb(), alloc_peb(), and wire_peb_fields().
 *
 * @return TEB pointer, or NULL on failure.
 */
void *setup_teb_peb(void)
{
    void *teb = alloc_teb(PAGE_SIZE);
    if (!teb) return NULL;

    void *peb = alloc_peb(PAGE_SIZE);
    if (!peb) {
        wine_munmap(teb, PAGE_SIZE);
        return NULL;
    }

    if (wire_peb_fields(teb, peb) < 0) {
        wine_munmap(peb, PAGE_SIZE);
        wine_munmap(teb, PAGE_SIZE);
        return NULL;
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
 * remap_stack_below_4gb — remap the stack to a 32-bit accessible address.
 *
 * Tries mmap at 0x7FFDC000 - commit (min 0x01000000), falls back to
 * MAP_FIXED at 0x02000000 if first attempt fails. Munmaps the old
 * allocation on success.
 *
 * @param  stack_base  original stack base (to be replaced)
 * @param  commit      stack commit size
 * @return  new base pointer, or NULL on failure
 */
static void *remap_stack_below_4gb(void *stack_base, size_t commit)
{
    uintptr_t stack_hint = 0x7FFDC000U - (uintptr_t)commit;
    /* Ensure we don't go below 0x01000000 to avoid conflicts */
    if (stack_hint < 0x01000000U) {
        stack_hint = 0x02000000U;
    }
    void *old_base = stack_base;
    stack_base = wine_mmap((void *)stack_hint, commit, PROT_READ|PROT_WRITE,
                      MAP_PRIVATE|MAP_ANONYMOUS|MAP_STACK|MAP_FIXED, -1, 0);
    if (stack_base == MAP_FAILED) {
        /* Try one more fallback location */
        wine_munmap(old_base, commit);
        stack_base = wine_mmap((void *)0x02000000U, commit, PROT_READ|PROT_WRITE,
                          MAP_PRIVATE|MAP_ANONYMOUS|MAP_STACK|MAP_FIXED, -1, 0);
        if (stack_base == MAP_FAILED) {
            wine_log_error("mmap stack (MAP_FIXED 32-bit)");
            return NULL;
        }
    } else {
        wine_munmap(old_base, commit);
    }
    return stack_base;
}

/**
 * Allocate the guest stack according to the PE's SizeOfStackReserve
 * and SizeOfStackCommit fields (with sensible minimums).
 *
 * For PE32, the stack is placed below 4GB so the guest can address it.
 *
 * @param  nt  pointer to the PE's IMAGE_NT_HEADERS
 * @return  top-of-stack pointer (aligned for ABI), or NULL
 */
void *setup_stack(IMAGE_NT_HEADERS *nt)
{
    uint64_t reserve = pe_stack_reserve(nt);
    uint64_t commit  = 0;

    /* For PE32+, get proper commit value */
    if (pe_is_pe64(nt)) {
        /* StackCommit is at a fixed offset in IMAGE_OPTIONAL_HEADER64 */
        /* It's right after SizeOfStackReserve in the struct */
        commit = nt->u.nt64.OptionalHeader.SizeOfStackCommit;
    } else {
        commit = nt->u.nt32.OptionalHeader.SizeOfStackCommit;
    }

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
    void *stack_base = wine_mmap(NULL, (size_t)commit, PROT_READ|PROT_WRITE,
                             MAP_PRIVATE|MAP_ANONYMOUS|MAP_STACK, -1, 0);
    if (stack_base == MAP_FAILED) {
        wine_log_error("mmap stack");
        return NULL;
    }

#if __SIZEOF_POINTER__ == 8
    /* For PE32, stack must be below 4GB */
    if (g_is_32bit && (uintptr_t)stack_base >= ADDR32_LIMIT) {
        void *new_base = remap_stack_below_4gb(stack_base, commit);
        if (!new_base) return NULL;
        stack_base = new_base;
        DEBUG("Stack remapped to %p for PE32", stack_base);
    }
#endif

    /* Top of stack (aligned to 16 bytes for Microsoft x64 ABI: rsp%16==8).
     * PE32 (stdcall) doesn't strictly require this, but it's harmless.
     * Sub 8 BEFORE alignment to ensure stack_top - 8 never exceeds
     * the mmap'd region. Without this, page-aligned bases + page-aligned
     * commits can push stack_top past the region boundary under ASLR. */
    uintptr_t stack_top = (uintptr_t)stack_base + (size_t)commit;
    stack_top = ((stack_top - 8) & ~(uintptr_t)15) + 8;  /* ABI requires rsp%16==8 */

    /* Print stack info */
    DEBUG("Stack: base=%p, top=%p, reserve=0x%lx, commit=0x%lx",
           stack_base, (void *)stack_top,
           (unsigned long)reserve, (unsigned long)commit);

    /* Store stack_base just below stack_top (guest pointer size) */
    uintptr_t sp = (uintptr_t)stack_top - (g_is_32bit ? 4 : 8);
    write_guest_ptr((void *)sp, 0, stack_base);
    g_stack_base = stack_base;
    g_stack_size = (size_t)commit;
    return (void *)stack_top;
}
