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

/* Guest-facing declarations (stdcall on i386, ms_abi on x86_64). */
KERNEL32_ABI
void *GetStdHandle(int nStdHandle);

KERNEL32_ABI
int WriteFile(void *hFile, const void *lpBuffer, uint32_t nNumberOfBytesToWrite,
              uint32_t *lpNumberOfBytesWritten, void *lpOverlapped);

KERNEL32_ABI
int ReadFile(void *hFile, void *lpBuffer, uint32_t nNumberOfBytesToRead,
             uint32_t *lpNumberOfBytesRead, void *lpOverlapped);

KERNEL32_ABI
void ExitProcess(uint32_t uExitCode);

KERNEL32_ABI
void *LoadLibraryA(const char *lpLibFileName);

/* SysV-compatible wrapper for LoadLibraryA (callable from native code / tests) */
void *_LoadLibraryA(const char *lpLibFileName);

KERNEL32_ABI
void *GetModuleHandleA(const char *lpModuleName);

/* SysV-compatible wrapper for GetModuleHandleA (callable from native code / tests) */
void *_GetModuleHandleA(const char *lpModuleName);

KERNEL32_ABI
void *GetModuleHandleW(const uint16_t *lpModuleName);

KERNEL32_ABI
void *GetProcAddress(void *hModule, const char *lpProcName);

/* SysV-compatible wrapper for GetProcAddress (callable from native code / tests) */
void *_GetProcAddress(void *hModule, const char *lpProcName);

KERNEL32_ABI
const char *GetCommandLineA(void);

KERNEL32_ABI
char *GetEnvironmentStringsA(void);

/* Character conversion / DBCS */
KERNEL32_ABI
int IsDBCSLeadByteEx(uint16_t code_page, uint8_t byte);

KERNEL32_ABI
int MultiByteToWideChar(uint32_t code_page, uint32_t dw_flags,
                        const char *lpMultiByteStr, int cbMultiByteChar,
                        void *lpWideCharStr, int cchWideChar);

KERNEL32_ABI
int WideCharToMultiByte(uint32_t code_page, uint32_t dw_flags,
                        const void *lpWideCharStr, int cchWideChar,
                        char *lpMultiByteStr, int cbMultiByteChar,
                        void *lpDefaultChar, void *lpUsedDefaultChar);

KERNEL32_ABI
int FreeLibraryA(void *hModule);

/* SysV-compatible wrapper for FreeLibraryA (callable from native code / tests) */
int _FreeLibraryA(void *hModule);

/* mingw-w64 imports "FreeLibrary" (no 'A' suffix) — alias to FreeLibraryA */
#define FreeLibrary FreeLibraryA

KERNEL32_ABI
__attribute__((noreturn))
void FreeLibraryAndExitThread(void *hModule, uint32_t exitCode);

KERNEL32_ABI
int lstrlenA(const char *lpString);

KERNEL32_ABI
char *lstrcpyA(char *dest, const char *src);
KERNEL32_ABI
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

KERNEL32_ABI
void GetSystemTimeAsFileTime(FILETIME *lpSystemTime);

KERNEL32_ABI
int QueryPerformanceCounter(LARGE_INTEGER *lpPerformanceCount);

KERNEL32_ABI
int QueryPerformanceFrequency(LARGE_INTEGER *lpFrequency);

/* ── Synchronization ────────────────────────────────────────── */

KERNEL32_ABI
void *CreateEventA(void *lpAttributes, int bManualReset, int bInitialState, const char *lpName);

KERNEL32_ABI
int SetEvent(void *hEvent);

KERNEL32_ABI
int ResetEvent(void *hEvent);

KERNEL32_ABI
uint64_t WaitForSingleObject(void *hHandle, uint32_t dwMilliseconds);

KERNEL32_ABI
void *CreateMutexA(void *lpAttributes, int bInitialOwner, const char *lpName);

KERNEL32_ABI
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

KERNEL32_ABI
void InitializeCriticalSection(CRITICAL_SECTION *cs);
KERNEL32_ABI
void EnterCriticalSection(CRITICAL_SECTION *cs);
KERNEL32_ABI
void LeaveCriticalSection(CRITICAL_SECTION *cs);
KERNEL32_ABI
void DeleteCriticalSection(CRITICAL_SECTION *cs);

/* ── Heap management ─────────────────────────────────────────── */

KERNEL32_ABI
void *HeapCreate(uint32_t flOptions, uintptr_t dwInitialSize, uintptr_t dwMaximumSize);

KERNEL32_ABI
void *HeapAlloc(void *hHeap, uint32_t dwFlags, uintptr_t dwBytes);

KERNEL32_ABI
int HeapFree(void *hHeap, uint32_t dwFlags, void *lpMem);

KERNEL32_ABI
void *HeapReAlloc(void *hHeap, uint32_t dwFlags, void *lpMem, uintptr_t dwBytes);

KERNEL32_ABI
void *GetProcessHeap(void);

KERNEL32_ABI
int HeapDestroy(void *hHeap);

KERNEL32_ABI
uintptr_t HeapSize(void *hHeap, uint32_t dwFlags, const void *lpMem);

/* Error handling */
KERNEL32_ABI
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

KERNEL32_ABI
void GetStartupInfoA(STARTUPINFOA *lpStartupInfo);

/* Exception handling */
KERNEL32_ABI
void *SetUnhandledExceptionFilter(void *callback);

/* Sleep */
KERNEL32_ABI
void Sleep(uint32_t dwMilliseconds);

/* Thread Local Storage */
KERNEL32_ABI
void *TlsGetValue(uint32_t dwTlsIndex);

/* Memory management */
KERNEL32_ABI
int VirtualProtect(void *lpAddress, uint32_t dwSize, uint32_t flNewProtect, uint32_t *lpflOldProtect);
KERNEL32_ABI
uint64_t VirtualQuery(void *lpAddress, void *lpBuffer, uint32_t dwLength);
KERNEL32_ABI
void *VirtualAlloc(void *lpAddress,
#if defined(__i386__)
                   uint32_t dwSize,
#else
                   uint64_t dwSize,
#endif
                   uint32_t flAllocationType, uint32_t flProtect);
KERNEL32_ABI
int VirtualFree(void *lpAddress,
#if defined(__i386__)
                uint32_t dwSize,
#else
                uint64_t dwSize,
#endif
                uint32_t dwFreeType);

/* File I/O */
KERNEL32_ABI
int CloseHandle(void *hObject);

#define INVALID_HANDLE_VALUE ((void *)(uintptr_t)(intptr_t)-1)

KERNEL32_ABI
void *CreateFileA(const char *lpFileName, uint32_t dwDesiredAccess,
                  uint32_t dwShareMode, void *lpSecurityAttributes,
                  uint32_t dwCreationDisposition, uint32_t dwFlagsAndAttributes,
                  void *hTemplateFile);
KERNEL32_ABI
int DeleteFileA(const char *lpFileName);

/* SEH handler */
KERNEL32_ABI
uint64_t __C_specific_handler(uint64_t exception_record, uint64_t establisher_frame,
                               uint64_t context_record, uint64_t dispatcher_context,
                               uint64_t image_base, uint64_t module_data,
                               uint64_t count, uint64_t handlers, uint64_t lang_handler,
                               uint64_t user_handler);

#endif /* MY_WINE_KERNEL32_H */
