#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>
#include <fcntl.h>

#include <sys/mman.h>
#include <sys/stat.h>
#include <pthread.h>

#include "include/ntdll.h"
#include "include/nt_constants.h"

/* ── Handle Table ───────────────────────────────────────────── */

#define HANDLE_TABLE_SIZE    256
#define STDIN_HANDLE         0x7FFFFFFF
#define STDOUT_HANDLE        0x7FFFFFFE
#define STDERR_HANDLE        0x7FFFFFFD

static struct {
    int        fd;
    uint8_t    used;
} handle_table[HANDLE_TABLE_SIZE];

static void init_handle_table(void)
{
    handle_table[0].fd   = STDIN_FILENO;
    handle_table[0].used = 1;
    handle_table[1].fd   = STDOUT_FILENO;
    handle_table[1].used = 1;
    handle_table[2].fd   = STDERR_FILENO;
    handle_table[2].used = 1;
}

static __attribute__((constructor)) void ntdll_init(void)
{
    init_handle_table();
}

/* Convert a Windows handle index to its Linux FD.
   Returns -1 on invalid handle. */
static int handle_to_fd(uint64_t handle)
{
    if (handle == STDIN_HANDLE)  return handle_table[0].fd;
    if (handle == STDOUT_HANDLE) return handle_table[1].fd;
    if (handle == STDERR_HANDLE) return handle_table[2].fd;

    unsigned idx = (unsigned)handle;
    if (idx >= HANDLE_TABLE_SIZE || !handle_table[idx].used)
        return -1;
    return handle_table[idx].fd;
}

/* Allocate a new Windows handle for a Linux FD.
   Returns the handle value (as uint64_t) or 0 on failure. */
static uint64_t fd_to_handle(int fd)
{
    unsigned idx;
    for (idx = 3; idx < HANDLE_TABLE_SIZE; idx++) {
        if (!handle_table[idx].used) {
            handle_table[idx].fd   = fd;
            handle_table[idx].used = 1;
            return (uint64_t)idx;
        }
    }
    return 0;
}

/* Remove a handle from the table. */
static void free_handle(uint64_t handle)
{
    if (handle == STDIN_HANDLE || handle == STDOUT_HANDLE || handle == STDERR_HANDLE)
        return;

    unsigned idx = (unsigned)handle;
    if (idx < HANDLE_TABLE_SIZE)
        handle_table[idx].used = 0;
}

/* ── Section Tracking ───────────────────────────────────────── */

#define MAX_SECTIONS 64

typedef struct {
    void     *base;
    size_t    size;
    int       fd;
    uint64_t  max_size;
} wine_section_t;

static wine_section_t sections[MAX_SECTIONS];
static int section_count = 0;

/* ── View Tracking ──────────────────────────────────────────── */

typedef struct {
    void  *base;
    size_t size;
} wine_view_t;

static wine_view_t views[MAX_SECTIONS];
static int view_count = 0;

/* ── Event Tracking ─────────────────────────────────────────── */

#define MAX_EVENTS 64

typedef struct {
    int handle;
    int signaled;
    int event_type; // 0 = Notification, 1 = Synchronization
} wine_event_t;

static wine_event_t events[MAX_EVENTS];
static int event_count = 0;

/* ── Thread Tracking ────────────────────────────────────────── */

#define MAX_THREADS 32

typedef struct {
    pthread_t tid;
    int suspended;
} wine_thread_t;

static wine_thread_t threads[MAX_THREADS];
static int thread_count = 0;

/* Map Windows PAGE_* protect values to Linux PROT_* flags */
static int map_protect(uint64_t protect)
{
    switch ((int)protect) {
    case  2: return PROT_READ;                    /* PAGE_READONLY */
    case  4: return PROT_READ | PROT_WRITE;       /* PAGE_READWRITE */
    case 16: return PROT_EXEC;                    /* PAGE_EXECUTE */
    case 32: return PROT_READ | PROT_EXEC;        /* PAGE_EXECUTE_READ */
    case 64: return PROT_READ | PROT_WRITE | PROT_EXEC; /* PAGE_EXECUTE_READWRITE */
    default: return PROT_READ | PROT_WRITE;       /* fallback */
    }
}

/* ── I/O Handlers ───────────────────────────────────────────── */

uint64_t handler_NtWriteFile(uint64_t file_handle, uint64_t event, uint64_t apc,
                             uint64_t context, uint64_t buffer, uint64_t length,
                             uint64_t byte_offset, uint64_t bytes_written)
{
    (void)event; (void)apc; (void)context; (void)byte_offset;

    int fd = handle_to_fd(file_handle);
    if (fd < 0) return STATUS_INVALID_HANDLE;

    const char *buf = (const char *)(uintptr_t)buffer;
    ssize_t n = write(fd, buf, (size_t)length);
    if (n < 0) return STATUS_UNSUCCESSFUL;

    if (bytes_written != 0)
        *(uint64_t *)(uintptr_t)bytes_written = (uint64_t)n;

    return STATUS_SUCCESS;
}

uint64_t handler_NtClose(uint64_t handle)
{
    int fd = handle_to_fd(handle);
    if (fd < 0) return STATUS_INVALID_HANDLE;

    close(fd);
    free_handle(handle);

    return STATUS_SUCCESS;
}

uint64_t handler_NtReadFile(uint64_t file_handle, uint64_t event, uint64_t apc,
                            uint64_t context, uint64_t buffer, uint64_t length,
                            uint64_t byte_offset, uint64_t bytes_read)
{
    (void)event; (void)apc; (void)context; (void)byte_offset;

    int fd = handle_to_fd(file_handle);
    if (fd < 0) return STATUS_INVALID_HANDLE;

    char *buf = (char *)(uintptr_t)buffer;
    ssize_t n = read(fd, buf, (size_t)length);
    if (n < 0) return STATUS_UNSUCCESSFUL;

    if (bytes_read != 0)
        *(uint64_t *)(uintptr_t)bytes_read = (uint64_t)n;

    return STATUS_SUCCESS;
}

uint64_t handler_NtTerminateProcess(uint64_t process_handle, uint64_t exit_status)
{
    if (process_handle != 0xFFFFFFFF)
        return STATUS_SUCCESS;

    exit((int)exit_status);
    return STATUS_SUCCESS; /* unreachable */
}

/* ── Stub Handlers ──────────────────────────────────────────── */

uint64_t handler_NtCallbackReturn(void)
{
    return STATUS_SUCCESS;
}

/*
 * handler_NtQueryInformationProcess
 *
 * PROCESS_BASIC_INFORMATION (x64, 40 bytes):
 *   uint64_t  ExitStatus
 *   uint64_t  PebBaseAddress
 *   uint64_t  AffinityMask
 *   int32_t   BasePriority
 *   uint8_t   pad[4]
 *   uint64_t  UniqueProcessId
 *   uint64_t  InheritedFromUniqueProcessId
 *
 * VM_COUNTERS (x64, 80 bytes) for ProcessWorkingSetSize:
 *   12 uint64_t fields
 */
uint64_t handler_NtQueryInformationProcess(uint64_t process_handle,
                                            uint64_t info_class,
                                            uint64_t buffer,
                                            uint64_t length,
                                            uint64_t return_length)
{
    (void)process_handle; /* we only know about ourselves */

    switch ((int)info_class) {
    case 0: { /* ProcessBasicInformation — 40 bytes */
        if (length < 40) return STATUS_BUFFER_TOO_SMALL;
        /*
         * Layout (40 bytes): ExitStatus(8) PebBaseAddr(8) Affinity(8)
         *                    BasePriority(8) PID(8) InheritedPID(8)
         */
        uint64_t *out = (uint64_t *)(uintptr_t)buffer;
        out[0] = 0;                         /* ExitStatus */
        out[1] = 0;                         /* PebBaseAddress — set by loader */
        out[2] = 1;                         /* AffinityMask */
        out[3] = 8;                         /* BasePriority */
        out[4] = getpid();                  /* UniqueProcessId */
        out[5] = getpid();                  /* InheritedFromUniqueProcessId */
        if (return_length) *(uint32_t *)(uintptr_t)return_length = 40;
        return STATUS_SUCCESS;
    }
    case 10: { /* ProcessWorkingSetSize — VM_COUNTERS, 80 bytes */
        if (length < 80) return STATUS_BUFFER_TOO_SMALL;
        uint64_t *out = (uint64_t *)(uintptr_t)buffer;
        /* All zeros is a valid approximation */
        memset(out, 0, 80);
        if (return_length) *(uint32_t *)(uintptr_t)return_length = 80;
        return STATUS_SUCCESS;
    }
    default:
        return STATUS_UNSUCCESSFUL;
    }
}

/*
 * handler_NtOpenFile
 *
 * OBJECT_ATTRIBUTES (x64, 40 bytes):
 *   uint32_t  Length           (24)
 *   int32_t   pad
 *   uint64_t  RootDirectory
 *   uint64_t  ObjectName  (pointer to UNICODE_STRING)
 *   uint32_t  Attributes
 *   int32_t   pad
 *   uint64_t  SecurityDescriptor
 *   uint64_t  SecurityQualityOfService
 *
 * UNICODE_STRING (x64, 12 bytes):
 *   uint16_t  Length
 *   uint16_t  MaximumLength
 *   uint64_t  Buffer     (pointer to wchar_t string)
 */
uint64_t handler_NtOpenFile(uint64_t *file_handle, uint64_t desired_access,
                            uint64_t object_attributes, uint64_t io_status_block,
                            uint64_t share_access, uint64_t dispose)
{
    (void)share_access;
    (void)dispose;

    const char *path = NULL;
    int oflags = 0;

    /* Map Windows desired_access to Linux open flags */
    if (desired_access & GENERIC_READ)
        oflags |= O_RDONLY;
    if (desired_access & GENERIC_WRITE)
        oflags |= O_RDWR;
    if (!(desired_access & GENERIC_READ) && !(desired_access & GENERIC_WRITE))
        oflags = O_RDONLY; /* default */

    /* Extract path from OBJECT_ATTRIBUTES if provided */
    if (object_attributes != 0) {
        /* Read ObjectName pointer (offset 16 in OBJECT_ATTRIBUTES) */
        uint64_t object_name_ptr = *(uint64_t *)((uintptr_t)object_attributes + 16);

        if (object_name_ptr != 0) {
            /* Read UNICODE_STRING (12 bytes) */
            uint16_t wcs_len = *(uint16_t *)(uintptr_t)object_name_ptr;
            const wchar_t *wcs = (const wchar_t *)(
                (uintptr_t)object_name_ptr + 12
            );
            if (wcs_len > 0) {
                /* Convert from UTF-16 to UTF-8 (assume ASCII for simplicity) */
                int max_len = wcs_len / 2;
                /* Allocate on heap for safety */
                char *utf8 = malloc((size_t)max_len + 1);
                if (utf8) {
                    int i;
                    for (i = 0; i < max_len && wcs[i] != 0; i++) {
                        if (wcs[i] < 0x80)
                            utf8[i] = (char)wcs[i];
                        else
                            utf8[i] = '?';
                    }
                    utf8[i] = '\0';
                    path = utf8;
                }
            }
        }
    }

    /* If no path extracted, fall back to /dev/null */
    const char *open_path = path ? path : "/dev/null";

    /* Open the file */
    int fd = open(open_path, oflags);
    if (fd < 0) {
        free((void *)path);
        return STATUS_UNSUCCESSFUL;
    }

    /* Store in handle table */
    uint64_t handle = fd_to_handle(fd);
    free((void *)path);
    if (handle == 0) {
        close(fd);
        return STATUS_UNSUCCESSFUL;
    }

    /* Write handle back */
    *file_handle = handle;

    /* Write IO_STATUS_BLOCK if provided (Information field at offset 8) */
    if (io_status_block != 0) {
        *(uint64_t *)((uintptr_t)io_status_block + 8) = 0; /* Information = 0 */
    }

    return STATUS_SUCCESS;
}

uint64_t handler_NtAllocateVirtualMemory(uint64_t process, uint64_t *base_address,
                                          uint64_t zero_bits, uint64_t *region_size,
                                          uint64_t allocation_type, uint64_t protect)
{
    (void)zero_bits;
    (void)allocation_type;

    if (process != 0xFFFFFFFF)
        return STATUS_ACCESS_DENIED;

    int prot = map_protect(protect);
    void *addr = NULL;

    if (base_address != 0 && *base_address != 0)
        addr = (void *)(uintptr_t)*base_address;

    void *result = mmap(addr, (size_t)*region_size, prot,
                        MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (result == MAP_FAILED)
        return STATUS_MEMORY_NOT_AVAILABLE;

    if (base_address != 0)
        *base_address = (uint64_t)(uintptr_t)result;

    /* mmap with MAP_ANONYMOUS returns exactly the requested size; */
    /* *region_size already holds the correct value. */

    return STATUS_SUCCESS;
}

uint64_t handler_NtFreeVirtualMemory(uint64_t process, uint64_t *base_address,
                                      uint64_t *region_size, uint64_t free_type)
{
    (void)free_type;

    if (process != 0xFFFFFFFF)
        return STATUS_ACCESS_DENIED;

    if (base_address == 0 || *base_address == 0)
        return STATUS_INVALID_PARAMETER;

    int ret = munmap((void *)(uintptr_t)*base_address, (size_t)*region_size);
    if (ret != 0)
        return STATUS_UNSUCCESSFUL;

    *base_address = 0;
    *region_size = 0;

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

uint64_t handler_NtMapViewOfSection(uint64_t section_handle, uint64_t process,
                                     uint64_t *base_address, uint64_t zero_bits,
                                     uint64_t commit_size, uint64_t *section_offset,
                                     uint64_t *view_size, uint64_t view_untyped,
                                     uint64_t allocation_type, uint64_t protect)
{
    (void)process;
    (void)zero_bits;
    (void)allocation_type;
    (void)view_untyped;

    /* Look up section by handle (handle = index+3) */
    unsigned idx = (unsigned)(section_handle - 3);
    if (section_handle < 3 || idx >= (unsigned)section_count)
        return STATUS_INVALID_HANDLE;

    wine_section_t *sec = &sections[idx];
    size_t view_sz = (commit_size != 0) ? (size_t)commit_size : sec->size;
    if (view_sz > sec->size)
        view_sz = sec->size;

    uint64_t offset = 0;
    if (section_offset != 0)
        offset = *section_offset;

    int prot = map_protect(protect);
    void *addr = NULL;
    if (base_address != 0 && *base_address != 0)
        addr = (void *)(uintptr_t)*base_address;

    void *result;
    if (sec->fd >= 0) {
        /* File-backed mapping */
        result = mmap(addr, view_sz, prot, MAP_PRIVATE, sec->fd, (off_t)offset);
    } else {
        /* Anonymous mapping — copy from section backing store */
        result = mmap(addr, view_sz, prot,
                      MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        if (result != MAP_FAILED && sec->base != NULL)
            memcpy(result, (const char *)sec->base + offset, view_sz);
    }

    if (result == MAP_FAILED)
        return STATUS_MEMORY_NOT_AVAILABLE;

    if (base_address != 0)
        *base_address = (uint64_t)(uintptr_t)result;

    if (view_size != 0)
        *view_size = (uint64_t)view_sz;

    /* Register the view so NtUnmapViewOfSection can find its full size */
    if (view_count < MAX_SECTIONS) {
        views[view_count].base = result;
        views[view_count].size = view_sz;
        view_count++;
    }

    return STATUS_SUCCESS;
}

/* Find a registered view by base address. Returns index or -1. */
static int find_view(void *base)
{
    int i;
    for (i = 0; i < view_count; i++) {
        if (views[i].base == base)
            return i;
    }
    return -1;
}

uint64_t handler_NtUnmapViewOfSection(uint64_t process, uint64_t base_address)
{
    (void)process;

    if (base_address == 0)
        return STATUS_INVALID_PARAMETER;

    /* Find the registered view for this base address */
    int idx = find_view((void *)(uintptr_t)base_address);
    if (idx < 0)
        return STATUS_INVALID_PARAMETER;

    size_t view_sz = views[idx].size;

    int ret = munmap(views[idx].base, view_sz);
    if (ret != 0)
        return STATUS_UNSUCCESSFUL;

    /* Remove the view from the registry */
    views[idx] = views[view_count - 1];
    view_count--;

    return STATUS_SUCCESS;
}

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

uint64_t handler_NtCreateSection(uint64_t *section_handle, uint64_t desired_access,
                                  uint64_t object_attributes, uint64_t *max_size,
                                  uint64_t page_protection, uint64_t section_attributes,
                                  uint64_t file_handle)
{
    (void)desired_access;
    (void)object_attributes;
    (void)section_attributes;

    if (section_count >= MAX_SECTIONS)
        return STATUS_MEMORY_NOT_AVAILABLE;

    int fd = -1;
    size_t size = (size_t)*max_size;

    if (file_handle != 0) {
        /* File-backed section */
        fd = handle_to_fd(file_handle);
        if (fd < 0)
            return STATUS_INVALID_HANDLE;
        /* Get actual file size */
        struct stat st;
        if (fstat(fd, &st) < 0)
            return STATUS_UNSUCCESSFUL;
        size = (size_t)st.st_size;
    }

    /* Allocate memory for the section with requested protection */
    int prot = map_protect(page_protection);
    void *base = mmap(NULL, size, prot,
                      MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (base == MAP_FAILED)
        return STATUS_MEMORY_NOT_AVAILABLE;

    if (fd >= 0) {
        /* Map file to a temporary address, copy into anonymous backing, unmap temp */
        void *temp_map = mmap(NULL, size, PROT_READ,
                              MAP_PRIVATE, fd, 0);
        if (temp_map == MAP_FAILED)
            return STATUS_UNSUCCESSFUL;
        memcpy(base, temp_map, size);
        munmap(temp_map, size);
    }

    int idx = section_count++;
    sections[idx].base     = base;
    sections[idx].size     = size;
    sections[idx].fd       = fd;
    sections[idx].max_size = *max_size;

    /* Return section handle (index+3 to avoid stdin/stdout/stderr) */
    *section_handle = (uint64_t)(idx + 3);

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
