#ifndef MY_WINE_KERNEL32_H
#define MY_WINE_KERNEL32_H

#include "wine_abi.h"
#include <stdint.h>

/* ── Standard Handle Constants ──────────────────────────────── */

#define STD_INPUT_HANDLE  ((int)-10)
#define STD_OUTPUT_HANDLE ((int)-11)
#define STD_ERROR_HANDLE  ((int)-12)

/* ── BOOL ───────────────────────────────────────────────────── */

#define TRUE  1
#define FALSE 0

/* ── Function Declarations ──────────────────────────────────── */

/* SysV-compatible versions for test code (non-guest callers).
 * Test code calls these directly instead of the ms_abi versions. */
void *_LoadLibraryA(const char *lpLibFileName);
void *_GetProcAddress(void *hModule, const char *lpProcName);
void *_GetModuleHandleA(const char *lpModuleName);
int _FreeLibraryA(void *hModule);

/* Guest-facing declarations (ms_abi — called from PE code) */
GUEST_ABI
void *GetStdHandle(int nStdHandle);

GUEST_ABI
int WriteFile(void *hFile, const void *lpBuffer, uint32_t nNumberOfBytesToWrite,
              uint32_t *lpNumberOfBytesWritten, void *lpOverlapped);

GUEST_ABI
int ReadFile(void *hFile, void *lpBuffer, uint32_t nNumberOfBytesToRead,
             uint32_t *lpNumberOfBytesRead, void *lpOverlapped);

GUEST_ABI
void ExitProcess(uint32_t uExitCode);

GUEST_ABI
void *LoadLibraryA(const char *lpLibFileName);

/* SysV-compatible wrapper for LoadLibraryA (callable from native code / tests) */
void *_LoadLibraryA(const char *lpLibFileName);

GUEST_ABI
void *GetModuleHandleA(const char *lpModuleName);

/* SysV-compatible wrapper for GetModuleHandleA (callable from native code / tests) */
void *_GetModuleHandleA(const char *lpModuleName);

GUEST_ABI
void *GetProcAddress(void *hModule, const char *lpProcName);

/* SysV-compatible wrapper for GetProcAddress (callable from native code / tests) */
void *_GetProcAddress(void *hModule, const char *lpProcName);

GUEST_ABI
const char *GetCommandLineA(void);

GUEST_ABI
char *GetEnvironmentStringsA(void);

/* Character conversion / DBCS */
GUEST_ABI
int IsDBCSLeadByteEx(uint16_t code_page, uint8_t byte);

GUEST_ABI
int MultiByteToWideChar(uint32_t code_page, uint32_t dw_flags,
                        const char *lpMultiByteStr, int cbMultiByteChar,
                        void *lpWideCharStr, int cchWideChar);

GUEST_ABI
int WideCharToMultiByte(uint32_t code_page, uint32_t dw_flags,
                        const void *lpWideCharStr, int cchWideChar,
                        char *lpMultiByteStr, int cbMultiByteChar,
                        void *lpDefaultChar, void *lpUsedDefaultChar);

GUEST_ABI
int FreeLibraryA(void *hModule);

/* SysV-compatible wrapper for FreeLibraryA (callable from native code / tests) */
int _FreeLibraryA(void *hModule);

/* mingw-w64 imports "FreeLibrary" (no 'A' suffix) — alias to FreeLibraryA */
#define FreeLibrary FreeLibraryA

GUEST_ABI
__attribute__((noreturn))
void FreeLibraryAndExitThread(void *hModule, uint32_t exitCode);

GUEST_ABI
int lstrlenA(const char *lpString);

GUEST_ABI
char *lstrcpyA(char *dest, const char *src);
GUEST_ABI
char *lstrcatA(char *dest, const char *src);

/* ── FILETIME / LARGE_INTEGER types ───────────────────────── */

typedef struct {
    uint32_t dwLowDateTime;
    uint32_t dwHighDateTime;
} FILETIME;

typedef struct {
    int64_t QuadPart;
} LARGE_INTEGER;

/* ── Time functions ─────────────────────────────────────────── */

GUEST_ABI
void GetSystemTimeAsFileTime(FILETIME *lpSystemTime);

GUEST_ABI
int QueryPerformanceCounter(LARGE_INTEGER *lpPerformanceCount);

GUEST_ABI
int QueryPerformanceFrequency(LARGE_INTEGER *lpFrequency);

/* ── Synchronization ────────────────────────────────────────── */

GUEST_ABI
void *CreateEventA(void *lpAttributes, int bManualReset, int bInitialState, const char *lpName);

GUEST_ABI
int SetEvent(void *hEvent);

GUEST_ABI
int ResetEvent(void *hEvent);

GUEST_ABI
uint64_t WaitForSingleObject(void *hHandle, uint32_t dwMilliseconds);

GUEST_ABI
void *CreateMutexA(void *lpAttributes, int bInitialOwner, const char *lpName);

GUEST_ABI
int ReleaseMutex(void *hMutex);

/* ── Additional kernel32 stubs ──────────────────────────────── */

/* Critical Section functions — 40-byte layout matching Windows x64 */
typedef struct {
    void    *DebugInfo;             // 0x00, 8 bytes
    int32_t  LockCount;             // 0x08, 4 bytes — -1=free, 0=owned
    int32_t  RecursionCount;        // 0x0C, 4 bytes
    uint64_t OwningThread;          // 0x10, 8 bytes — thread ID
    uint64_t LockSemaphore;         // 0x18, 8 bytes — kernel EVENT handle
    uint64_t SpinCount;             // 0x20, 8 bytes
} CRITICAL_SECTION;                 // total 40 bytes

GUEST_ABI
void InitializeCriticalSection(CRITICAL_SECTION *cs);
GUEST_ABI
void EnterCriticalSection(CRITICAL_SECTION *cs);
GUEST_ABI
void LeaveCriticalSection(CRITICAL_SECTION *cs);
GUEST_ABI
void DeleteCriticalSection(CRITICAL_SECTION *cs);

/* ── Heap management ─────────────────────────────────────────── */

GUEST_ABI
void *HeapCreate(uint32_t flOptions, uint64_t dwInitialSize, uint64_t dwMaximumSize);

GUEST_ABI
void *HeapAlloc(void *hHeap, uint32_t dwFlags, uint64_t dwBytes);

GUEST_ABI
int HeapFree(void *hHeap, uint32_t dwFlags, void *lpMem);

GUEST_ABI
void *HeapReAlloc(void *hHeap, uint32_t dwFlags, void *lpMem, uint64_t dwBytes);

GUEST_ABI
void *GetProcessHeap(void);

GUEST_ABI
int HeapDestroy(void *hHeap);

GUEST_ABI
uint64_t HeapSize(void *hHeap, uint32_t dwFlags, const void *lpMem);

/* Error handling */
GUEST_ABI
uint32_t GetLastError(void);

/* Startup info */
typedef struct {
    uint32_t cb;
    void *lpReserved;
    void *hDesktop;
    void *hHeap;
    void *hProcess;
    void *hThread;
    uint32_t dwX, dwY;
    uint32_t dwXSize, dwYSize;
    uint32_t dwCharX, dwCharY;
    uint32_t dwFillAttribute;
    uint32_t dwFlags;
    uint16_t wShowWindow;
    uint16_t cbReserved2;
    void *lpReserved2;
    void *hStdInput;
    void *hStdOutput;
    void *hStdError;
} STARTUPINFOA;

GUEST_ABI
void GetStartupInfoA(STARTUPINFOA *lpStartupInfo);

/* Exception handling */
GUEST_ABI
void *SetUnhandledExceptionFilter(void *callback);

/* Sleep */
GUEST_ABI
void Sleep(uint32_t dwMilliseconds);

/* Thread Local Storage */
GUEST_ABI
void *TlsGetValue(uint32_t dwTlsIndex);

/* Memory management */
GUEST_ABI
int VirtualProtect(void *lpAddress, uint32_t dwSize, uint32_t flNewProtect, uint32_t *lpflOldProtect);
GUEST_ABI
uint64_t VirtualQuery(void *lpAddress, void *lpBuffer, uint32_t dwLength);
GUEST_ABI
void *VirtualAlloc(void *lpAddress,
#if defined(__i386__)
                   uint32_t dwSize,
#else
                   uint64_t dwSize,
#endif
                   uint32_t flAllocationType, uint32_t flProtect);
GUEST_ABI
int VirtualFree(void *lpAddress,
#if defined(__i386__)
                uint32_t dwSize,
#else
                uint64_t dwSize,
#endif
                uint32_t dwFreeType);

/* File I/O */
GUEST_ABI
int CloseHandle(void *hObject);

#define INVALID_HANDLE_VALUE ((void *)(uintptr_t)(intptr_t)-1)

GUEST_ABI
void *CreateFileA(const char *lpFileName, uint32_t dwDesiredAccess,
                  uint32_t dwShareMode, void *lpSecurityAttributes,
                  uint32_t dwCreationDisposition, uint32_t dwFlagsAndAttributes,
                  void *hTemplateFile);
GUEST_ABI
int DeleteFileA(const char *lpFileName);

/* SEH handler */
GUEST_ABI
uint64_t __C_specific_handler(uint64_t exception_record, uint64_t establisher_frame,
                               uint64_t context_record, uint64_t dispatcher_context,
                               uint64_t image_base, uint64_t module_data,
                               uint64_t count, uint64_t handlers, uint64_t lang_handler,
                               uint64_t user_handler);

#endif /* MY_WINE_KERNEL32_H */
