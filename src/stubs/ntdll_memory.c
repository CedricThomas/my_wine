/*
 * ntdll_memory.c — Memory management syscall handlers
 *
 * NtAllocateVirtualMemory, NtFreeVirtualMemory, NtCreateSection,
 * NtMapViewOfSection, NtUnmapViewOfSection
 */

#include <stdint.h>
#include <stddef.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include "handler_abi.h"
#include "ntdll_priv.h"
#include "../syscall/syscalls_inline.h"
#include "include/abi_wrappers.h"
#include "include/common.h"

/* ── Section / View storage ────────────────────────────────────── */

wine_section_t sections[MAX_SECTIONS];
int section_count = 0;

wine_view_t views[MAX_SECTIONS];
int view_count = 0;

/* Map Windows PAGE_* protect values to Linux PROT_* flags */
int map_protect(uint64_t protect)
{
    switch ((int)protect) {
    case PAGE_READONLY:          return PROT_READ;
    case PAGE_READWRITE:         return PROT_READ | PROT_WRITE;
    case PAGE_EXECUTE:           return PROT_EXEC;
    case PAGE_EXECUTE_READ:      return PROT_READ | PROT_EXEC;
    case PAGE_EXECUTE_READWRITE: return PROT_READ | PROT_WRITE | PROT_EXEC;
    default: return PROT_READ | PROT_WRITE;       /* fallback */
    }
}

HANDLER
uint64_t handler_NtAllocateVirtualMemory(uint64_t process, uint64_t *base_address,
                                          uint64_t zero_bits, uint64_t *region_size,
                                          uint64_t allocation_type, uint64_t protect)
{
    (void)zero_bits;
    (void)allocation_type;

    if (process != HANDLE_CURRENT_PROCESS)
        return STATUS_ACCESS_DENIED;

    int prot = map_protect(protect);
    void *addr = NULL;

    if (base_address != 0 && *base_address != 0)
        addr = (void *)(uintptr_t)*base_address;

    void *result = sysv_mmap(addr, (size_t)*region_size, prot,
                        MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (result == MAP_FAILED)
        return STATUS_MEMORY_NOT_AVAILABLE;

    if (base_address != 0)
        *base_address = (uint64_t)(uintptr_t)result;

    /* sysv_mmap with MAP_ANONYMOUS returns exactly the requested size; */
    /* *region_size already holds the correct value. */

    return STATUS_SUCCESS;
}

HANDLER
uint64_t handler_NtFreeVirtualMemory(uint64_t process, uint64_t *base_address,
                                      uint64_t *region_size, uint64_t free_type)
{
    (void)free_type;

    if (process != HANDLE_CURRENT_PROCESS)
        return STATUS_ACCESS_DENIED;

    if (base_address == 0 || *base_address == 0)
        return STATUS_INVALID_PARAMETER;

    long res = INLINE_SYSCALL_MUNMAP((void *)(uintptr_t)*base_address, (size_t)*region_size);
    if (res != 0)
        return STATUS_UNSUCCESSFUL;

    *base_address = 0;
    *region_size = 0;

    return STATUS_SUCCESS;
}

HANDLER
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
        result = sysv_mmap(addr, view_sz, prot, MAP_PRIVATE, sec->fd, (off_t)offset);
    } else {
        /* Anonymous mapping — copy from section backing store */
        result = sysv_mmap(addr, view_sz, prot,
                      MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        if (result != MAP_FAILED && sec->base != NULL)
            sysv_memcpy(result, (const char *)sec->base + offset, view_sz);
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
int find_view(void *base)
{
    int i;
    for (i = 0; i < view_count; i++) {
        if (views[i].base == base)
            return i;
    }
    return -1;
}

HANDLER
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

    long res = INLINE_SYSCALL_MUNMAP(views[idx].base, view_sz);
    if (res != 0)
        return STATUS_UNSUCCESSFUL;

    /* Remove the view from the registry */
    views[idx] = views[view_count - 1];
    view_count--;

    return STATUS_SUCCESS;
}

HANDLER
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
        /* Get actual file size via fstat syscall */
        struct stat st;
        long res = INLINE_SYSCALL_FSTAT(fd, &st);
        if (res < 0)
            return STATUS_UNSUCCESSFUL;
        size = (size_t)st.st_size;
    }

    /* Allocate memory for the section with requested protection */
    int prot = map_protect(page_protection);
    void *base = sysv_mmap(NULL, size, prot,
                      MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (base == MAP_FAILED)
        return STATUS_MEMORY_NOT_AVAILABLE;

    if (fd >= 0) {
        /* Map file to a temporary address, copy into anonymous backing, unmap temp */
        void *temp_map = sysv_mmap(NULL, size, PROT_READ,
                              MAP_PRIVATE, fd, 0);
        if (temp_map == MAP_FAILED)
            return STATUS_UNSUCCESSFUL;
        sysv_memcpy(base, temp_map, size);
        INLINE_SYSCALL_MUNMAP(temp_map, size);
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
