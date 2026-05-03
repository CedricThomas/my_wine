#ifndef MY_WINE_NTDLL_H
#define MY_WINE_NTDLL_H

#include <stdint.h>

// --- NT Status Codes ---

#define STATUS_SUCCESS              0x00000000
#define STATUS_UNSUCCESSFUL         0xC0000001
#define STATUS_INVALID_ADDRESS      0xC0000004
#define STATUS_INVALID_HANDLE       0xC0000008
#define STATUS_INVALID_PARAMETER    0xC000000D
#define STATUS_ACCESS_DENIED        0xC0000022
#define STATUS_BUFFER_TOO_SMALL     0xC0000023
#define STATUS_MEMORY_NOT_AVAILABLE 0xC0000098

// --- NT Syscall Handler Signatures ---
// x86_64 Windows calling convention: first 4 args in RCX, RDX, R8, R9;
// additional args on the stack. All handlers return uint64_t (STATUS).

// NtCallbackReturn (0x05) — no-op
uint64_t handler_NtCallbackReturn(void);

// NtQueryInformationProcess (0x07)
uint64_t handler_NtQueryInformationProcess(uint64_t process_handle, uint64_t info_class, uint64_t buffer, uint64_t length, uint64_t return_length);

// NtClose (0x0F)
uint64_t handler_NtClose(uint64_t handle);

// NtAllocateVirtualMemory (0x18)
uint64_t handler_NtAllocateVirtualMemory(uint64_t process, uint64_t *base_address, uint64_t zero_bits, uint64_t *region_size, uint64_t allocation_type, uint64_t protect);

// NtFreeVirtualMemory (0x19)
uint64_t handler_NtFreeVirtualMemory(uint64_t process, uint64_t *base_address, uint64_t *region_size, uint64_t free_type);

// NtGetContextThread (0x24)
uint64_t handler_NtGetContextThread(uint64_t thread_handle, uint64_t context);

// NtSetContextThread (0x26)
uint64_t handler_NtSetContextThread(uint64_t thread_handle, uint64_t context);

// NtMapViewOfSection (0x28)
uint64_t handler_NtMapViewOfSection(uint64_t section_handle, uint64_t process, uint64_t *base_address, uint64_t zero_bits, uint64_t commit_size, uint64_t *section_offset, uint64_t *view_size, uint64_t view_untyped, uint64_t allocation_type, uint64_t protect);

// NtUnmapViewOfSection (0x29)
uint64_t handler_NtUnmapViewOfSection(uint64_t process, uint64_t base_address);

// NtTerminateProcess (0x2A)
uint64_t handler_NtTerminateProcess(uint64_t process_handle, uint64_t exit_status);

// NtReadFile (0x3C)
uint64_t handler_NtReadFile(uint64_t file_handle, uint64_t event, uint64_t apc, uint64_t context, uint64_t buffer, uint64_t length, uint64_t byte_offset, uint64_t bytes_read);

// NtWriteFile (0x3D)
uint64_t handler_NtWriteFile(uint64_t file_handle, uint64_t event, uint64_t apc, uint64_t context, uint64_t buffer, uint64_t length, uint64_t byte_offset, uint64_t bytes_written);

// NtCreateEvent (0x48)
uint64_t handler_NtCreateEvent(uint64_t *event_handle, uint64_t desired_access, uint64_t object_attributes, uint64_t event_type, uint64_t initial_state);

// NtCreateSection (0x4A)
uint64_t handler_NtCreateSection(uint64_t *section_handle, uint64_t desired_access, uint64_t object_attributes, uint64_t *max_size, uint64_t page_protection, uint64_t section_attributes, uint64_t file_handle);

// NtCreateThreadEx (0x4E)
uint64_t handler_NtCreateThreadEx(uint64_t *thread_handle, uint64_t desired_access, uint64_t object_attributes, uint64_t process_handle, uint64_t start_routine, uint64_t argument, uint64_t create_flags, uint64_t stack_size, uint64_t commit_size, uint64_t attribute, uint64_t attr_list);

// NtOpenFile (0x4F)
uint64_t handler_NtOpenFile(uint64_t *file_handle, uint64_t desired_access, uint64_t object_attributes, uint64_t io_status_block, uint64_t share_access, uint64_t dispose);

#endif // MY_WINE_NTDLL_H
