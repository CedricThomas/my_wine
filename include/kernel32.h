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

/* Critical Section functions */
typedef struct {
    void *DebugInfo;
    int LockCount;
    int RecursionCount;
    void *OwningThread;
    void *SpinCount;
} CRITICAL_SECTION;

__attribute__((ms_abi))
void InitializeCriticalSection(CRITICAL_SECTION *cs);
__attribute__((ms_abi))
void EnterCriticalSection(CRITICAL_SECTION *cs);
__attribute__((ms_abi))
void LeaveCriticalSection(CRITICAL_SECTION *cs);
__attribute__((ms_abi))
void DeleteCriticalSection(CRITICAL_SECTION *cs);

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
