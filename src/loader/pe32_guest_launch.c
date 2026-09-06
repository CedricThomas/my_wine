/*
 * pe32_guest_launch.c -- PE32 guest bootstrap and final handoff.
 *
 * This module owns the transition-sensitive path from prepared PE32 process
 * state to an actual guest jump.
 */

#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>

#include "include/common.h"
#include "include/nt_constants.h"
#include "include/syscall/dispatcher_entry.h"
#include "include/syscall/thunk_gen.h"
#include "loader_state.h"
#include "pe32_entry_resolve.h"
#include "pe32_guest_launch.h"
#include "pe32_process.h"
#include "teb_peb.h"
#include "crash_handlers.h"
#include "../syscall/syscalls_inline.h"

extern void pe32_run_guest(uint32_t entry_abs, void *stack_top) __attribute__((noreturn));
extern void pe32_guest_return_exit(void);

static const char err_teb[]        = "my_wine32: failed to allocate TEB\n";
static const char err_peb[]        = "my_wine32: failed to allocate PEB\n";
static const char err_unix_stack[] = "my_wine32: failed to setup UNIX stack\n";
static const char err_thunks[]     = "my_wine32: failed to generate thunks\n";
static const char err_stack[]      = "my_wine32: failed to setup guest stack\n";
static const char err_fs[]         = "my_wine32: set_thread_area (FS→TEB) failed\n";

struct exception_registration_record {
    uint32_t next;
    uint32_t handler;
};

static struct exception_registration_record g_seh_frame = {
    .next = 0xFFFFFFFF,
    .handler = 0,
};

void *pe32_init_teb_or_exit(void *peb)
{
    void *teb = INLINE_SYSCALL_MMAP((void *)(uintptr_t)TEB32_FIXED_ADDR,
                                    PAGE_SIZE, PROT_READ | PROT_WRITE,
                                    MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED,
                                    -1, 0);
    if (teb == MAP_FAILED) {
        INLINE_SYSCALL_WRITE_ERR(err_teb, sizeof(err_teb) - 1);
        INLINE_SYSCALL_EXIT_GROUP(1);
    }

    memset(teb, 0, PAGE_SIZE);
    init_teb32_fields(teb, peb);

    g_seh_frame.handler = (uint32_t)(uintptr_t)seh_crash_handler;
    *(uint32_t *)((uint8_t *)teb + TEB32_SEH_CHAIN) =
        (uint32_t)(uintptr_t)&g_seh_frame;

    {
        extern char **environ;
        *(uint32_t *)((uint8_t *)teb + TEB32_ENV_PTR) =
            (uint32_t)(uintptr_t)environ;
    }

    *(uint32_t *)((uint8_t *)teb + TEB32_CLIENT_ID_PID) =
        (uint32_t)INLINE_SYSCALL_GETPID();
    *(uint32_t *)((uint8_t *)teb + TEB32_CLIENT_ID_TID) =
        (uint32_t)INLINE_SYSCALL_GETTID();

    return teb;
}

void *pe32_init_peb_or_exit(void *base)
{
    void *peb = INLINE_SYSCALL_MMAP((void *)(uintptr_t)PEB32_FIXED_ADDR,
                                    PAGE_SIZE, PROT_READ | PROT_WRITE,
                                    MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED,
                                    -1, 0);
    if (peb == MAP_FAILED) {
        INLINE_SYSCALL_WRITE_ERR(err_peb, sizeof(err_peb) - 1);
        INLINE_SYSCALL_EXIT_GROUP(1);
    }

    memset(peb, 0, PAGE_SIZE);
    init_peb32_fields(peb, base);
    return peb;
}

void pe32_prepare_dispatch_or_exit(void)
{
    if (setup_unix_stack() != 0) {
        INLINE_SYSCALL_WRITE_ERR(err_unix_stack, sizeof(err_unix_stack) - 1);
        INLINE_SYSCALL_EXIT_GROUP(1);
    }

    if (generate_all_thunks() == NULL) {
        INLINE_SYSCALL_WRITE_ERR(err_thunks, sizeof(err_thunks) - 1);
        INLINE_SYSCALL_EXIT_GROUP(1);
    }

    install_crash_signal_handlers();
}

void *pe32_setup_guest_stack_or_exit(IMAGE_NT_HEADERS *nt)
{
    void *stack_top = setup_stack(nt);

    if (stack_top == NULL) {
        INLINE_SYSCALL_WRITE_ERR(err_stack, sizeof(err_stack) - 1);
        INLINE_SYSCALL_EXIT_GROUP(1);
    }

    return stack_top;
}

__attribute__((noreturn))
void pe32_setup_fs_and_jump(void *teb, const char *pe_path,
                            uint32_t entry_abs, void *stack_top)
{
    uintptr_t aligned_sp;
    uint8_t *sp;
    pe32_entry_type_t entry_type;
    uint32_t zero_len;

    ensure_argv_setup(pe_path);

    aligned_sp = ((uintptr_t)stack_top & ~(uintptr_t)15) - 4;
    sp = (uint8_t *)aligned_sp;
    entry_type = pe32_get_entry_type();
    zero_len = (entry_type == PE32_ENTRY_TYPE_MAIN) ? 16 : 20;
    __builtin_memset(sp, 0, zero_len);

    *(uint32_t *)(sp + 0) = (uint32_t)(uintptr_t)pe32_guest_return_exit;

    if (entry_type == PE32_ENTRY_TYPE_MAIN) {
        *(uint32_t *)(sp + 4) = 1;
        *(uint32_t *)(sp + 8) = pe32_argv_ptr();
        *(uint32_t *)(sp + 12) = pe32_envp_ptr();
    } else {
        *(uint32_t *)(sp + 4) = (uint32_t)(uintptr_t)g_loader.image_base;
        *(uint32_t *)(sp + 8) = 0;
        *(uint32_t *)(sp + 12) =
            (uint32_t)(uintptr_t)((uint8_t *)g_argv_page + 0);
        *(uint32_t *)(sp + 16) = 5;
    }

    {
        uint16_t host_fs;
        __asm__ volatile("mov %%fs, %0" : "=r"(host_fs));
        loader_set_host_fs_selector(host_fs);
    }

    struct modify_ldt_ldt_s ldt = {
        .entry_number = -1,
        .base_addr = (unsigned int)(uintptr_t)teb,
        .limit = 0xFFFFF,
        .seg_32bit = 1,
        .contents = 0,
        .read_exec_only = 0,
        .limit_in_pages = 0,
        .seg_not_present = 0,
        .usable = 1,
        .garbage = 0
    };
    long ldt_rc = INLINE_SYSCALL_SET_THREAD_AREA(&ldt);
    if (ldt_rc < 0) {
        INLINE_SYSCALL_WRITE_ERR(err_fs, sizeof(err_fs) - 1);
    } else {
        uint16_t fs_sel = ((uint16_t)ldt.entry_number << 3) | 3;
        __asm__ volatile("mov %0, %%fs" : : "r"(fs_sel) : "memory");
    }

    pe32_run_guest(entry_abs, stack_top);
}
