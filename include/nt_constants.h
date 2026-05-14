#ifndef MY_WINE_NT_CONSTANTS_H
#define MY_WINE_NT_CONSTANTS_H

/*
 * nt_constants.h — Named NT syscall numbers, TEB/PEB offsets
 *
 * Centralized constants for the my_wine PE loader.
 * All magic numbers extracted from the syscall dispatcher, TEB/PEB setup,
 * and thunk generation code.
 *
 * =====================================================================
 * ARCHITECTURE NOTE: x86_64 and x86_32
 * =====================================================================
 *
 * TEB field offsets come in two variants:
 *   TEB64_* for x86_64 (PE32+ / AMD64)
 *   TEB32_* for x86_32 (PE32 / I386)
 *
 * PEB field offsets come in two variants:
 *   PEB64_* for x86_64 (PE32+ / AMD64)
 *   PEB32_* for x86_32 (PE32 / I386)
 *
 * Backward-compat aliases (TEB_*, PEB_*) map to the 64-bit variants.
 *
 * These offsets will differ on ARM64 and other architectures.
 * This code will not work on non-x86 architectures without updating
 * these offsets.
 * =====================================================================
 */

/* ── NT syscall numbers (x86_64) ──────────────────────────────── */
/*
 * Version: Windows 10+ (specifically tested on Windows 10 64-bit).
 *
 * Syscall numbers are NOT stable across Windows versions — they differ
 * between Win7, Win8.1, Win10, and Win11.  These numbers must be updated
 * if the target Windows version changes.
 */

#define NT_SYSCALL_CALLBACK_RETURN       0x05  /* NtCallbackReturn */
#define NT_SYSCALL_QUERY_INFO_PROCESS    0x07  /* NtQueryInformationProcess */
#define NT_SYSCALL_CLOSE                 0x0F  /* NtClose */
#define NT_SYSCALL_ALLOC_VM              0x18  /* NtAllocateVirtualMemory */
#define NT_SYSCALL_FREE_VM               0x19  /* NtFreeVirtualMemory */
#define NT_SYSCALL_GET_CTX_THREAD        0x24  /* NtGetContextThread */
#define NT_SYSCALL_SET_CTX_THREAD        0x26  /* NtSetContextThread */
#define NT_SYSCALL_MAP_VIEW              0x28  /* NtMapViewOfSection */
#define NT_SYSCALL_UNMAP_VIEW            0x29  /* NtUnmapViewOfSection */
#define NT_SYSCALL_TERMINATE_PROCESS     0x2A  /* NtTerminateProcess */
#define NT_SYSCALL_READ_FILE             0x3C  /* NtReadFile */
#define NT_SYSCALL_WRITE_FILE            0x3D  /* NtWriteFile */
#define NT_SYSCALL_CREATE_EVENT          0x48  /* NtCreateEvent */
#define NT_SYSCALL_CREATE_SECTION        0x4A  /* NtCreateSection */
#define NT_SYSCALL_CREATE_THREAD_EX      0x4E  /* NtCreateThreadEx */
#define NT_SYSCALL_OPEN_FILE             0x4F  /* NtOpenFile */
#define NT_SYSCALL_QUERY_SYSTEM_TIME      0x09  /* NtQuerySystemTime */
#define NT_SYSCALL_DELAY_EXECUTION        0x1A  /* NtDelayExecution */
#define NT_SYSCALL_RELEASE_MUTEX          0x1E  /* NtReleaseMutex */
#define NT_SYSCALL_CREATE_MUTEX           0x44  /* NtCreateMutex */
#define NT_SYSCALL_QUERY_PERFORMANCE_COUNTER 0x55  /* NtQueryPerformanceCounter */
#define NT_SYSCALL_QUERY_PERFORMANCE_FREQUENCY 0x56 /* NtQueryPerformanceFrequency */
#define NT_SYSCALL_SET_EVENT              0x5C  /* NtSetEvent */
#define NT_SYSCALL_RESET_EVENT            0x5E  /* NtResetEvent */
#define NT_SYSCALL_WAIT_FOR_SINGLE_OBJECT 0x03  /* NtWaitForSingleObject */

/* ── TEB field offsets (x86_64 Windows) ───────────────────────── */

#define TEB64_SEH_CHAIN      0x00   /* NextExceptionHandler */
#define TEB64_TEB_SELF_REF   0x08   /* Self pointer */
#define TEB64_THREAD_PTR     0x48   /* ThreadPointer field in x86_64 TEB */
#define TEB64_PEB_PTR        0x60   /* PEB pointer */

/* ── TEB field offsets (x86_32 Windows) ───────────────────────── */

#define TEB32_SEH_CHAIN      0x00   /* NextExceptionHandler */
#define TEB32_TEB_SELF_REF   0x04   /* Self pointer */
#define TEB32_THREAD_PTR     0x24   /* ThreadPointer field in x86_32 TEB */
#define TEB32_PEB_PTR        0x30   /* PEB pointer */
#define TEB32_FIBER_DATA     0x10   /* FiberData field in x86_32 TEB */
#define TEB32_GDI_TEB_OFFSET 0x18   /* GdiTebOffset field in x86_32 TEB */
#define TEB32_GDI_PROCESS_LOCAL 0x1C  /* GdiProcessLocals field in x86_32 TEB */

/* ── PEB field offsets (x86_64 Windows) ───────────────────────── */

#define PEB64_BEING_DEBUGGED 0x002  /* BeingDebugged flag */
#define PEB64_IMAGE_BASE     0x008  /* ImageBaseAddress */
#define PEB64_PROCESS_HEAP   0x030  /* ProcessHeap */

#ifndef PEB64_LDR
#define PEB64_LDR            0x18   /* PEB_LDR_DATA pointer offset in PEB */
#endif

/* ── PEB field offsets (x86_32 Windows) ───────────────────────── */

#define PEB32_BEING_DEBUGGED 0x002  /* BeingDebugged flag */
#define PEB32_IMAGE_BASE     0x008  /* ImageBaseAddress */
#define PEB32_PROCESS_HEAP   0x03C  /* ProcessHeap — Win7+ (0x03C); WinXP/2000 (0x018) */
                                        /* Ref: Microsoft TEB/PEB layout (MSDN: ntdef.h, PEB structure) */
#define PEB32_LDR            0x00C  /* PEB_LDR_DATA pointer offset in PEB */

/* ── Backward-compat aliases (default to 64-bit) ──────────────── */

#define TEB_SEH_CHAIN        TEB64_SEH_CHAIN
#define TEB_TEB_SELF_REF     TEB64_TEB_SELF_REF
#define TEB_THREAD_PTR       TEB64_THREAD_PTR
#define TEB_PEB_PTR          TEB64_PEB_PTR

#define PEB_BEING_DEBUGGED   PEB64_BEING_DEBUGGED
#define PEB_IMAGE_BASE       PEB64_IMAGE_BASE
#define PEB_PROCESS_HEAP     PEB64_PROCESS_HEAP
#define PEB_LDR              PEB64_LDR

/* ── Data Directory entries ───────────────────────────────────── */

#ifndef DIRECTORY_ENTRY_EXPORT
#define DIRECTORY_ENTRY_EXPORT            0
#endif
#ifndef DIRECTORY_ENTRY_TLS
#define DIRECTORY_ENTRY_TLS               9
#endif
#ifndef DIRECTORY_ENTRY_EXCEPTION
#define DIRECTORY_ENTRY_EXCEPTION         3  /* IMAGE_DIRECTORY_ENTRY_EXCEPTION */
#endif
#ifndef IMAGE_NUMBEROF_DIRECTORY_ENTRIES
#define IMAGE_NUMBEROF_DIRECTORY_ENTRIES  16
#endif

/* ── Windows access mask constants ──────────────────────────── */

#define GENERIC_READ          0x80000000
#define GENERIC_WRITE         0x40000000

/* ── Windows error codes ────────────────────────────────────── */

#define ERROR_SUCCESS                0
#define ERROR_ACCESS_DENIED          5
#define ERROR_INVALID_PARAMETER     87
#define ERROR_INSUFFICIENT_BUFFER  122
#define ERROR_FILE_NOT_FOUND       2

/* ── NT status codes ────────────────────────────────────────── */

#define STATUS_ACCESS_VIOLATION   0xC0000005

/* ── Address space limits ─────────────────────────────────────── */

#define ADDR32_LIMIT             0x100000000ULL  /* 4 GB — upper bound of 32-bit address space */

/* ── PE32 default image base ─────────────────────────────────── */

#ifndef PE32_DEFAULT_IMAGE_BASE
#define PE32_DEFAULT_IMAGE_BASE  0x00400000ULL  /* Default base for PE32 (x86) images */
#endif

#endif /* MY_WINE_NT_CONSTANTS_H */
