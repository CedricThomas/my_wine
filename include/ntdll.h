#ifndef MY_WINE_NTDLL_H
#define MY_WINE_NTDLL_H

#include <stdint.h>

/* ── Windows-type wrappers for readability ─────────────────── */
typedef uint64_t        HANDLE;
typedef uint64_t        PVOID;
typedef uint64_t        ULONG_PTR;
typedef uint64_t        ULONG;
typedef uint64_t        NTSTATUS;
typedef uint16_t        USHORT;
typedef uint8_t         UCHAR;
typedef uint64_t        DWORD64;
typedef uint32_t        DWORD;
typedef uint64_t        BOOL;

/* ── Windows page protection constants ─────────────────────── */
#define PAGE_NOACCESS          0x01
#define PAGE_READONLY          0x02
#define PAGE_READWRITE         0x04
#define PAGE_WRITECOPY         0x08
#define PAGE_EXECUTE           0x10
#define PAGE_EXECUTE_READ      0x20
#define PAGE_EXECUTE_READWRITE 0x40
#define PAGE_EXECUTE_WRITECOPY 0x80

// --- Memory allocation constants ---

#define MEM_COMMIT       0x1000
#define MEM_RESERVE      0x2000
#define MEM_DECOMMIT     0x4000
#define MEM_RELEASE      0x8000
#define MEM_PRIVATE      0x20000
#define MEM_MAPPED       0x40000

// --- NT Status Codes ---

#define STATUS_SUCCESS              0x00000000
#define STATUS_UNSUCCESSFUL         0xC0000001
#define STATUS_INVALID_ADDRESS      0xC0000004
#define STATUS_INVALID_HANDLE       0xC0000008
#define STATUS_INVALID_PARAMETER    0xC000000D
#define STATUS_ACCESS_DENIED        0xC0000022
#define STATUS_ACCESS_VIOLATION     0xC0000005
#define STATUS_BUFFER_TOO_SMALL     0xC0000023
#define STATUS_MEMORY_NOT_AVAILABLE 0xC0000098

// --- NT Syscall Handler Signatures ---
// x86_64 Windows calling convention: first 4 args in RCX, RDX, R8, R9;
// additional args on the stack. All handlers return NTSTATUS.
// Note: internal implementations (ntdll_*.c) still use raw uint64_t/uint32_t
// for pointer arithmetic — these typed declarations are header-only wrappers
// for readability.

// NtCallbackReturn (0x05) — no-op
NTSTATUS handler_NtCallbackReturn(void);

// NtQueryInformationProcess (0x07)
NTSTATUS handler_NtQueryInformationProcess(HANDLE process_handle, ULONG info_class, PVOID buffer, ULONG length, PVOID return_length);

// NtClose (0x0F)
NTSTATUS handler_NtClose(HANDLE handle);

// NtAllocateVirtualMemory (0x18)
NTSTATUS handler_NtAllocateVirtualMemory(HANDLE process, PVOID *base_address, ULONG zero_bits, PVOID *region_size, ULONG allocation_type, ULONG protect);

// NtFreeVirtualMemory (0x19)
NTSTATUS handler_NtFreeVirtualMemory(HANDLE process, PVOID *base_address, PVOID *region_size, ULONG free_type);

// NtGetContextThread (0x24)
NTSTATUS handler_NtGetContextThread(HANDLE thread_handle, PVOID context);

// NtSetContextThread (0x26)
NTSTATUS handler_NtSetContextThread(HANDLE thread_handle, PVOID context);

// NtMapViewOfSection (0x28)
NTSTATUS handler_NtMapViewOfSection(HANDLE section_handle, HANDLE process, PVOID *base_address, ULONG zero_bits, ULONG_PTR commit_size, PVOID *section_offset, PVOID *view_size, ULONG view_untyped, ULONG allocation_type, ULONG protect);

// NtUnmapViewOfSection (0x29)
NTSTATUS handler_NtUnmapViewOfSection(HANDLE process, PVOID base_address);

// NtTerminateProcess (0x2A)
NTSTATUS handler_NtTerminateProcess(HANDLE process_handle, NTSTATUS exit_status);

// NtReadFile (0x3C)
NTSTATUS handler_NtReadFile(HANDLE file_handle, HANDLE event, PVOID apc, PVOID context, PVOID buffer, ULONG length, ULONG byte_offset, PVOID bytes_read);

// NtWriteFile (0x3D)
NTSTATUS handler_NtWriteFile(HANDLE file_handle, HANDLE event, PVOID apc, PVOID context, PVOID buffer, ULONG length, ULONG byte_offset, PVOID bytes_written);

// NtCreateEvent (0x48)
NTSTATUS handler_NtCreateEvent(PVOID *event_handle, ULONG desired_access, PVOID object_attributes, ULONG event_type, BOOL initial_state);

// NtCreateSection (0x4A)
NTSTATUS handler_NtCreateSection(PVOID *section_handle, ULONG desired_access, PVOID object_attributes, PVOID *max_size, ULONG page_protection, ULONG section_attributes, HANDLE file_handle);

// NtCreateThreadEx (0x4E)
NTSTATUS handler_NtCreateThreadEx(PVOID *thread_handle, ULONG desired_access, PVOID object_attributes, HANDLE process_handle, PVOID start_routine, PVOID argument, ULONG create_flags, ULONG_PTR stack_size, ULONG_PTR commit_size, PVOID attribute, PVOID attr_list);

// NtOpenFile (0x4F)
NTSTATUS handler_NtOpenFile(PVOID *file_handle, ULONG desired_access, PVOID object_attributes, PVOID io_status_block, ULONG share_access, ULONG dispose);

/* ── Windows struct definitions (packed) ─────────────────── */
#pragma pack(push, 1)
typedef struct {
    uint16_t Length;
    uint16_t MaximumLength;
    uint64_t Buffer;
} UNICODE_STRING;

typedef struct {
    uint32_t  Length;
    uint32_t  _pad;
    uint64_t  RootDirectory;
    uint64_t  ObjectName;
    uint32_t  Attributes;
    uint32_t  _pad2;
    uint64_t  SecurityDescriptor;
    uint64_t  SecurityQos;
} OBJECT_ATTRIBUTES;
#pragma pack(pop)

#endif // MY_WINE_NTDLL_H
