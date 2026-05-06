#ifndef MY_WINE_KERNEL32_H
#define MY_WINE_KERNEL32_H

#include <stdint.h>

/* ── Standard Handle Constants ──────────────────────────────── */

#define STD_INPUT_HANDLE  ((int)-10)
#define STD_OUTPUT_HANDLE ((int)-11)
#define STD_ERROR_HANDLE  ((int)-12)

/* ── BOOL ───────────────────────────────────────────────────── */

#define TRUE  1
#define FALSE 0

/* ── Function Declarations ──────────────────────────────────── */

__attribute__((ms_abi))
void *GetStdHandle(int nStdHandle);

__attribute__((ms_abi))
int WriteFile(void *hFile, const void *lpBuffer, uint32_t nNumberOfBytesToWrite,
              uint32_t *lpNumberOfBytesWritten, void *lpOverlapped);

__attribute__((ms_abi))
int ReadFile(void *hFile, void *lpBuffer, uint32_t nNumberOfBytesToRead,
             uint32_t *lpNumberOfBytesRead, void *lpOverlapped);

__attribute__((ms_abi))
void ExitProcess(uint32_t uExitCode);

__attribute__((ms_abi))
void *GetProcAddress(void *hModule, const char *lpProcName);

__attribute__((ms_abi))
void *LoadLibraryA(const char *lpLibFileName);

__attribute__((ms_abi))
void *GetModuleHandleA(const char *lpModuleName);

__attribute__((ms_abi))
const char *GetCommandLineA(void);

__attribute__((ms_abi))
char *GetEnvironmentStringsA(void);

/* Character conversion / DBCS */
__attribute__((ms_abi))
int IsDBCSLeadByteEx(uint16_t code_page, uint8_t byte);

__attribute__((ms_abi))
int MultiByteToWideChar(uint32_t code_page, uint32_t dw_flags,
                        const char *lpMultiByteStr, int cbMultiByteChar,
                        void *lpWideCharStr, int cchWideChar);

__attribute__((ms_abi))
int WideCharToMultiByte(uint32_t code_page, uint32_t dw_flags,
                        const void *lpWideCharStr, int cchWideChar,
                        char *lpMultiByteStr, int cbMultiByteChar,
                        void *lpDefaultChar, void *lpUsedDefaultChar);

__attribute__((ms_abi))
int FreeLibraryA(void *hModule);

/* mingw-w64 imports "FreeLibrary" (no 'A' suffix) — alias to FreeLibraryA */
#define FreeLibrary FreeLibraryA

__attribute__((ms_abi))
__attribute__((noreturn))
void FreeLibraryAndExitThread(void *hModule, uint32_t exitCode);

__attribute__((ms_abi))
int lstrlenA(const char *lpString);

/* ── FILETIME / LARGE_INTEGER types ───────────────────────── */

typedef struct {
    uint32_t dwLowDateTime;
    uint32_t dwHighDateTime;
} FILETIME;

typedef struct {
    int64_t QuadPart;
} LARGE_INTEGER;

/* ── Time functions ─────────────────────────────────────────── */

__attribute__((ms_abi))
void GetSystemTimeAsFileTime(FILETIME *lpSystemTime);

__attribute__((ms_abi))
int QueryPerformanceCounter(LARGE_INTEGER *lpPerformanceCount);

__attribute__((ms_abi))
int QueryPerformanceFrequency(LARGE_INTEGER *lpFrequency);

/* ── Synchronization ────────────────────────────────────────── */

__attribute__((ms_abi))
void *CreateEventA(void *lpAttributes, int bManualReset, int bInitialState, const char *lpName);

__attribute__((ms_abi))
int SetEvent(void *hEvent);

__attribute__((ms_abi))
int ResetEvent(void *hEvent);

__attribute__((ms_abi))
uint64_t WaitForSingleObject(void *hHandle, uint32_t dwMilliseconds);

__attribute__((ms_abi))
void *CreateMutexA(void *lpAttributes, int bInitialOwner, const char *lpName);

__attribute__((ms_abi))
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

__attribute__((ms_abi))
void InitializeCriticalSection(CRITICAL_SECTION *cs);
__attribute__((ms_abi))
void EnterCriticalSection(CRITICAL_SECTION *cs);
__attribute__((ms_abi))
void LeaveCriticalSection(CRITICAL_SECTION *cs);
__attribute__((ms_abi))
void DeleteCriticalSection(CRITICAL_SECTION *cs);

/* ── Heap management ─────────────────────────────────────────── */

__attribute__((ms_abi))
void *HeapCreate(uint32_t flOptions, uint64_t dwInitialSize, uint64_t dwMaximumSize);

__attribute__((ms_abi))
void *HeapAlloc(void *hHeap, uint32_t dwFlags, uint64_t dwBytes);

__attribute__((ms_abi))
int HeapFree(void *hHeap, uint32_t dwFlags, void *lpMem);

__attribute__((ms_abi))
void *HeapReAlloc(void *hHeap, uint32_t dwFlags, void *lpMem, uint64_t dwBytes);

__attribute__((ms_abi))
void *GetProcessHeap(void);

__attribute__((ms_abi))
int HeapDestroy(void *hHeap);

__attribute__((ms_abi))
uint64_t HeapSize(void *hHeap, uint32_t dwFlags, const void *lpMem);

/* Error handling */
__attribute__((ms_abi))
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

__attribute__((ms_abi))
void GetStartupInfoA(STARTUPINFOA *lpStartupInfo);

/* Exception handling */
__attribute__((ms_abi))
void *SetUnhandledExceptionFilter(void *callback);

/* Sleep */
__attribute__((ms_abi))
void Sleep(uint32_t dwMilliseconds);

/* Thread Local Storage */
__attribute__((ms_abi))
void *TlsGetValue(uint32_t dwTlsIndex);

/* Memory management */
__attribute__((ms_abi))
int VirtualProtect(void *lpAddress, uint32_t dwSize, uint32_t flNewProtect, uint32_t *lpflOldProtect);
__attribute__((ms_abi))
uint64_t VirtualQuery(void *lpAddress, void *lpBuffer, uint32_t dwLength);

/* SEH handler */
__attribute__((ms_abi))
uint64_t __C_specific_handler(uint64_t exception_record, uint64_t establisher_frame,
                               uint64_t context_record, uint64_t dispatcher_context,
                               uint64_t image_base, uint64_t module_data,
                               uint64_t count, uint64_t handlers, uint64_t lang_handler,
                               uint64_t user_handler);

#endif /* MY_WINE_KERNEL32_H */
