/*
 * ntdll_objects.c — Object & thread syscall handlers
 *
 * NtCreateEvent, NtCreateThreadEx, NtGetContextThread, NtSetContextThread
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <pthread.h>

#include "ntdll_priv.h"

/* ── Event / Thread storage ────────────────────────────────────── */

wine_event_t events[MAX_EVENTS];
int event_count = 0;

wine_thread_t threads[MAX_THREADS];
int thread_count = 0;

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

static void *thread_wrapper(void *arg)
{
    uint64_t routine = (uint64_t)(uintptr_t)((void **)arg)[0];
    uint64_t param   = (uint64_t)(uintptr_t)((void **)arg)[1];
    free(arg);
    void (*fn)(void *) = (void (*)(void *))routine;
    fn((void *)(uintptr_t)param);
    return NULL;
}

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

    /* Prepare arguments for the wrapper */
    void **args = malloc(sizeof(void *) * 2);
    args[0] = (void *)(uintptr_t)start_routine;
    args[1] = (void *)(uintptr_t)argument;

    /* Use default pthread stack (custom stack allocation + immediate munmap
       after pthread_create was a race — the thread could segfault on a freed
       stack.  Stack size parameter is acknowledged but not yet implemented.) */
    (void)stack_size;

    pthread_t tid;
    int ret = pthread_create(&tid, NULL, thread_wrapper, args);

    if (ret != 0) {
        free(args);
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
        pthread_detach(tid);
        return STATUS_MEMORY_NOT_AVAILABLE;
    }

    int slot = thread_count++;
    threads[slot].tid       = tid;
    threads[slot].suspended = (create_flags & 4) ? 1 : 0; /* CREATE_SUSPENDED */

    if (thread_handle != 0)
        *thread_handle = handle;

    return STATUS_SUCCESS;
}

uint64_t handler_NtGetContextThread(uint64_t thread_handle, uint64_t context)
{
    (void)thread_handle;
    /* Windows CONTEXT is 500+ bytes; zero it out as placeholder */
    if (context != 0)
        memset((void *)(uintptr_t)context, 0, 544);
    return STATUS_SUCCESS;
}

uint64_t handler_NtSetContextThread(uint64_t thread_handle, uint64_t context)
{
    (void)thread_handle; (void)context;
    return STATUS_SUCCESS;
}
