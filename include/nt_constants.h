#ifndef MY_WINE_NT_CONSTANTS_H
#define MY_WINE_NT_CONSTANTS_H

/*
 * nt_constants.h — Named NT syscall numbers, TEB/PEB offsets
 *
 * Centralized constants for the my_wine PE loader.
 * All magic numbers extracted from the syscall dispatcher, TEB/PEB setup,
 * and thunk generation code.
 */

/* ── NT syscall numbers (x86_64) ──────────────────────────────── */

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

#define TEB_SEH_CHAIN        0x00   /* NextExceptionHandler */
#define TEB_TEB_SELF_REF     0x08   /* Self pointer */
#define TEB_THREAD_PTR       0x30   /* Thread pointer (fake self-ref) */
#define TEB_PEB_PTR          0x60   /* PEB pointer */

/* ── PEB field offsets (x86_64 Windows) ───────────────────────── */

#define PEB_BEING_DEBUGGED   0x002  /* BeingDebugged flag */
#define PEB_IMAGE_BASE       0x008  /* ImageBaseAddress */
#define PEB_PROCESS_HEAP     0x030  /* ProcessHeap */

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

#endif /* MY_WINE_NT_CONSTANTS_H */
