#ifndef MY_WINE_NT_CONSTANTS_H
#define MY_WINE_NT_CONSTANTS_H

/*
 * nt_constants.h — Named NT syscall numbers, TEB/PEB offsets, Wine syscall offset
 *
 * Centralized constants for the my_wine PE loader.
 * All magic numbers extracted from the syscall dispatcher, TEB/PEB setup,
 * and Wine thunk generation code.
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

/* ── TEB field offsets (x86_64 Windows) ───────────────────────── */

#define TEB_SEH_CHAIN        0x00   /* NextExceptionHandler */
#define TEB_TEB_SELF_REF     0x08   /* Self pointer */
#define TEB_THREAD_PTR       0x30   /* Thread pointer (fake self-ref) */
#define TEB_PEB_PTR          0x60   /* PEB pointer */

/* ── PEB field offsets (x86_64 Windows) ───────────────────────── */

#define PEB_BEING_DEBUGGED   0x002  /* BeingDebugged flag */
#define PEB_IMAGE_BASE       0x008  /* ImageBaseAddress */

#endif /* MY_WINE_NT_CONSTANTS_H */
