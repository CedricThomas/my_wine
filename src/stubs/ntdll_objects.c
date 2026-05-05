/*
 * ntdll_objects.c — Object & thread syscall handlers
 *
 * NtCreateEvent, NtCreateThreadEx, NtGetContextThread, NtSetContextThread
 */

#define _GNU_SOURCE

#include <stdint.h>
#include <stddef.h>
#include <unistd.h>
#include <sched.h>
#include <sys/mman.h>
#include <sys/syscall.h>
#include <signal.h>
#include "../syscalls_inline.h"
#include "handler_abi.h"
#include "ntdll_priv.h"

#ifndef PAGE_SIZE
#define PAGE_SIZE 4096
#endif

/* ── Event / Thread storage ────────────────────────────────────── */

wine_event_t events[MAX_EVENTS];
int event_count = 0;

wine_thread_t threads[MAX_THREADS];
int thread_count = 0;

HANDLER
uint64_t handler_NtCreateEvent(uint64_t *event_handle, uint64_t desired_access,
                               uint64_t object_attributes, uint64_t event_type,
                               uint64_t initial_state)
{
    (void)desired_access;
    (void)object_attributes;

    if (event_count >= MAX_EVENTS)
        return STATUS_MEMORY_NOT_AVAILABLE;

    /* Find a free slot in the handle table for this event */
    uint64_t handle = 0;
    {
        unsigned idx;
        for (idx = 3; idx < HANDLE_TABLE_SIZE; idx++) {
            if (!handle_table[idx].used) {
                handle_table[idx].fd   = -1; /* not an fd, marks event slot */
                handle_table[idx].used = 1;
                handle = (uint64_t)idx;
                break;
            }
        }
    }

    if (handle == 0)
        return STATUS_MEMORY_NOT_AVAILABLE;

    int slot = event_count++;
    events[slot].handle   = (int)handle;
    events[slot].signaled = (initial_state != 0) ? 1 : 0;
    events[slot].event_type = (int)event_type;

    if (event_handle != 0)
        *event_handle = handle;

    return STATUS_SUCCESS;
}

static void thread_wrapper(void *arg)
{
    uint64_t routine = (uint64_t)(uintptr_t)((void **)arg)[0];
    uint64_t param   = (uint64_t)(uintptr_t)((void **)arg)[1];
    /* munmap the args page — safe since we extracted values above */
    (void)INLINE_SYSCALL_MUNMAP(arg, PAGE_SIZE);
    void (*fn)(void *) = (void (*)(void *))routine;
    fn((void *)(uintptr_t)param);
    /* Exit this thread — clone'd thread terminates with exit() */
    INLINE_SYSCALL_EXIT(0);
}

HANDLER
uint64_t handler_NtCreateThreadEx(uint64_t *thread_handle, uint64_t desired_access,
                                   uint64_t object_attributes, uint64_t process_handle,
                                   uint64_t start_routine, uint64_t argument,
                                   uint64_t create_flags, uint64_t stack_size,
                                   uint64_t commit_size, uint64_t attribute,
                                   uint64_t attr_list)
{
    (void)desired_access;
    (void)object_attributes;
    (void)process_handle;
    (void)commit_size;
    (void)attribute;
    (void)attr_list;

    if (thread_count >= MAX_THREADS)
        return STATUS_MEMORY_NOT_AVAILABLE;

    /* Prepare arguments for the wrapper — use mmap instead of malloc */
    void **args = (void **)INLINE_SYSCALL_MMAP(NULL, PAGE_SIZE,
        PROT_READ | PROT_WRITE,
        MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if ((long)(uintptr_t)args < 0)
        return STATUS_MEMORY_NOT_AVAILABLE;
    args[0] = (void *)(uintptr_t)start_routine;
    args[1] = (void *)(uintptr_t)argument;

    /* Use default clone stack (custom stack allocation via stack_size
       parameter is acknowledged but not yet implemented.) */
    (void)stack_size;

    /* Use clone() syscall directly — no pthread, no glibc.
     * The clone'd thread inherits the same address space (CLONE_VM)
     * and same GS base. It will share the parent's TEB, which is
     * acceptable in the single-process model. */
    long tid = INLINE_SYSCALL_CLONE(
        CLONE_VM | CLONE_FS | CLONE_FILES | CLONE_SIGHAND | SIGCHLD,
        0,   /* child_stack = 0 → kernel allocates default stack */
        NULL, /* parent_tid */
        NULL, /* child_tid */
        thread_wrapper, args
    );

    if (tid < 0) {
        (void)INLINE_SYSCALL_MUNMAP(args, PAGE_SIZE);
        return STATUS_UNSUCCESSFUL;
    }

    /* Find a free slot in the handle table */
    uint64_t handle = 0;
    {
        unsigned idx;
        for (idx = 3; idx < HANDLE_TABLE_SIZE; idx++) {
            if (!handle_table[idx].used) {
                handle_table[idx].fd   = -1; /* not an fd, marks thread slot */
                handle_table[idx].used = 1;
                handle = (uint64_t)idx;
                break;
            }
        }
    }

    if (handle == 0) {
        INLINE_SYSCALL_KILL(tid, SIGKILL);
        return STATUS_MEMORY_NOT_AVAILABLE;
    }

    int slot = thread_count++;
    threads[slot].tid       = (pthread_t)(uintptr_t)tid;
    threads[slot].suspended = (create_flags & 4) ? 1 : 0; /* CREATE_SUSPENDED */

    if (thread_handle != 0)
        *thread_handle = handle;

    return STATUS_SUCCESS;
}

HANDLER
uint64_t handler_NtGetContextThread(uint64_t thread_handle, uint64_t context)
{
    (void)thread_handle;
    /* Windows CONTEXT is 500+ bytes; zero it out as placeholder */
    if (context != 0)
        __builtin_memset((void *)(uintptr_t)context, 0, 544);
    return STATUS_SUCCESS;
}

HANDLER
uint64_t handler_NtSetContextThread(uint64_t thread_handle, uint64_t context)
{
    (void)thread_handle; (void)context;
    return STATUS_SUCCESS;
}
