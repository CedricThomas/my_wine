#define _GNU_SOURCE

#include "kernel32_priv.h"

#define LMEM_FIXED 0x0000
#define GMEM_FIXED 0x0000

/* VirtualAlloc tracking for VirtualFree(MEM_RELEASE, size=0). */
#define MAX_VM_ALLOCS 64

typedef struct {
    uint64_t base;
    uint64_t size;
} vm_alloc_entry_t;

static vm_alloc_entry_t vm_allocs[MAX_VM_ALLOCS];
static int vm_alloc_count = 0;

static int vm_alloc_find(uint64_t base)
{
    int i;
    for (i = 0; i < vm_alloc_count; i++) {
        if (vm_allocs[i].base == base)
            return i;
    }
    return -1;
}

static void vm_alloc_add(uint64_t base, uint64_t size)
{
    if (vm_alloc_count >= MAX_VM_ALLOCS)
        return;
    vm_allocs[vm_alloc_count].base = base;
    vm_allocs[vm_alloc_count].size = size;
    vm_alloc_count++;
}

static void vm_alloc_remove(int idx)
{
    if (idx < 0 || idx >= vm_alloc_count)
        return;
    vm_allocs[idx] = vm_allocs[vm_alloc_count - 1];
    vm_alloc_count--;
}

/* MEMORY_BASIC_INFORMATION (x64, 48 bytes) */
typedef struct {
    void    *BaseAddress;
    void    *AllocationBase;
    uint32_t AllocationProtect;
    uint32_t __unused1;
    uint64_t RegionSize;
    uint32_t State;
    uint32_t Protect;
    uint32_t Type;
    uint32_t __unused2;
} MEMORY_BASIC_INFORMATION;

KERNEL32_STUB
void *LocalAlloc(uint32_t uFlags, uintptr_t uBytes)
{
    (void)uFlags;
    return FORCE_PTR_RETURN(HeapAlloc(GetProcessHeap(), LMEM_FIXED, uBytes));
}

KERNEL32_STUB
void *GlobalAlloc(uint32_t uFlags, uintptr_t uBytes)
{
    (void)uFlags;
    return FORCE_PTR_RETURN(HeapAlloc(GetProcessHeap(), GMEM_FIXED, uBytes));
}

KERNEL32_STUB
void *LocalFree(void *hMem)
{
    if (!hMem)
        return FORCE_PTR_RETURN(NULL);
    return FORCE_PTR_RETURN(HeapFree(GetProcessHeap(), 0, hMem) ? NULL : hMem);
}

KERNEL32_STUB
int VirtualProtect(void *lpAddress, uint32_t dwSize, uint32_t flNewProtect, uint32_t *lpflOldProtect)
{
    int prot = map_protect(flNewProtect);

    if (lpflOldProtect) {
        *lpflOldProtect = flNewProtect;
    }

    size_t page_size = PAGE_SIZE;
    void *page_start = (void *)((uintptr_t)lpAddress & ~(page_size - 1));
    uintptr_t offset = (uintptr_t)lpAddress - (uintptr_t)page_start;
    size_t total_size = offset + dwSize;
    size_t aligned_size = (total_size + page_size - 1) & ~(size_t)(page_size - 1);

    if (sysv_mprotect(page_start, aligned_size, prot) != 0) {
        write_to_stderr("my_wine: VirtualProtect: mprotect failed\n");
        g_last_error = ERROR_ACCESS_DENIED;
        return 0;
    }
    return 1;
}

KERNEL32_STUB
uint64_t VirtualQuery(void *lpAddress, void *lpBuffer, uint32_t dwLength)
{
    if (!lpAddress || !lpBuffer) {
        return 0;
    }

    if (dwLength < sizeof(MEMORY_BASIC_INFORMATION)) {
        g_last_error = ERROR_INSUFFICIENT_BUFFER;
        return 0;
    }

    MEMORY_BASIC_INFORMATION *mbi = (MEMORY_BASIC_INFORMATION *)lpBuffer;
    mbi->BaseAddress = lpAddress;
    mbi->AllocationBase = lpAddress;
    mbi->AllocationProtect = PAGE_READWRITE;
    mbi->RegionSize = PAGE_SIZE;
    mbi->State = MEM_COMMIT;
    mbi->Protect = PAGE_READWRITE;
    mbi->Type = MEM_PRIVATE;

    return sizeof(MEMORY_BASIC_INFORMATION);
}

KERNEL32_STUB
void *VirtualAlloc(void *lpAddress,
#if defined(__i386__)
                   uint32_t dwSize,
#else
                   uint64_t dwSize,
#endif
                   uint32_t flAllocationType, uint32_t flProtect)
{
    if (dwSize == 0)
        return FORCE_PTR_RETURN(NULL);

    uint64_t base = (uint64_t)(uintptr_t)lpAddress;
    uint64_t region_size = (uint64_t)dwSize;

    uint64_t status = handler_NtAllocateVirtualMemory(
        HANDLE_CURRENT_PROCESS,
        &base,
        0,
        &region_size,
        (uint64_t)flAllocationType,
        (uint64_t)flProtect
    );

    if (status != STATUS_SUCCESS)
        return FORCE_PTR_RETURN(NULL);

    void *result = (void *)(uintptr_t)base;
    vm_alloc_add(base, region_size);
    return FORCE_PTR_RETURN(result);
}

KERNEL32_STUB
int VirtualFree(void *lpAddress,
#if defined(__i386__)
                uint32_t dwSize,
#else
                uint64_t dwSize,
#endif
                uint32_t dwFreeType)
{
    if (lpAddress == NULL) {
        return 0;
    }

    uint64_t base = (uint64_t)(uintptr_t)lpAddress;
    uint64_t region_size = (uint64_t)dwSize;

    if ((dwFreeType & 0x8000) && region_size == 0) {
        int idx = vm_alloc_find(base);
        if (idx >= 0) {
            region_size = vm_allocs[idx].size;
        } else {
            int vidx = find_view(lpAddress);
            if (vidx >= 0)
                region_size = (uint64_t)ko_view(vidx)->size;
            else
                return 0;
        }
    }

    uint64_t status = handler_NtFreeVirtualMemory(
        HANDLE_CURRENT_PROCESS,
        &base,
        &region_size,
        (uint64_t)dwFreeType
    );

    if (status == STATUS_SUCCESS) {
        int idx = vm_alloc_find(base);
        if (idx >= 0)
            vm_alloc_remove(idx);
    }

    return status == STATUS_SUCCESS;
}
